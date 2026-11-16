using System.Diagnostics;
using System.Net;
using System.Text.Json;
using Ds3Latency;
using Microsoft.Diagnostics.Tracing.Parsers;
using Microsoft.Diagnostics.Tracing.Parsers.Kernel;
using Microsoft.Diagnostics.Tracing.Session;

static class Program
{
    static readonly JsonSerializerOptions JsonOptions=new(){PropertyNamingPolicy=JsonNamingPolicy.SnakeCaseLower};
    static long MonoNs()=> (long)(Stopwatch.GetTimestamp()*(1_000_000_000.0/Stopwatch.Frequency));
    static string Wall()=>DateTimeOffset.Now.ToString("O");
    static void AtomicJson(string path,object value)
    {
        var temp=path+".tmp";var json=JsonSerializer.Serialize(value,JsonOptions);
        // Readers on Windows may briefly hold a handle without delete-sharing.
        for(var attempt=0;;attempt++)try
        {
            File.WriteAllText(temp,json);File.Move(temp,path,true);return;
        }
        catch(Exception e)when((e is IOException or UnauthorizedAccessException)&&attempt<40)
        {Thread.Sleep(25);}
    }
    static void PersistFailure(object value,StreamWriter log,string latestPath,string statusPath)
    {
        var json=JsonSerializer.Serialize(value,JsonOptions);
        Console.Error.WriteLine(json);
        // Status replacement may itself be the failure. Preserve the error independently.
        try{log.WriteLine(json);}
        catch(Exception e){Console.Error.WriteLine("Failure journal write failed: "+e);}
        foreach(var path in new[]{latestPath,statusPath})try{AtomicJson(path,value);}
        catch(Exception e){Console.Error.WriteLine("Failure snapshot write failed for "+path+": "+e);}
    }

    static int Main(string[] args)
    {
        if(args.Contains("--self-test"))return SelfTest();
        if(args.Contains("--self-test-io"))return SelfTestIo();
        string? Get(string flag){var i=Array.IndexOf(args,flag);return i>=0&&i+1<args.Length?args[i+1]:null;}
        var output=Path.GetFullPath(Get("--output")??Path.Combine(AppContext.BaseDirectory,"capture"));
        var stop=Path.GetFullPath(Get("--stop-file")??Path.Combine(output,"STOP"));
        var hours=double.Parse(Get("--hours")??"10",System.Globalization.CultureInfo.InvariantCulture);
        var fixedPid=int.TryParse(Get("--game-pid"),out var gp)?gp:0;
        var peerFile=Get("--peers-file");
        var steamOptIn=args.Contains("--include-steam-for-known-peers");
        if(!double.IsFinite(hours)||hours<=0||hours>24*30)throw new ArgumentOutOfRangeException("--hours");
        Directory.CreateDirectory(output);
        var started=Stopwatch.GetTimestamp();var startedWall=Wall();
        var statusPath=Path.Combine(output,"status.json");
        var latestPath=Path.Combine(output,"latest.json");
        var sessionName="CopydogLatencyPassive_"+Environment.ProcessId+"_"+DateTime.UtcNow.Ticks;
        long snapshotSequence=0;
        string SnapshotId()=>sessionName+":"+(++snapshotSequence);
        using var log=new StreamWriter(Path.Combine(output,"network.jsonl"),append:true){AutoFlush=true};
        void WriteStatus(object value)
        {
            var json=JsonSerializer.Serialize(value,JsonOptions);
            log.WriteLine(json);AtomicJson(latestPath,value);AtomicJson(statusPath,value);
        }
        object Failure(string reason,string detail)=>new {type="network_snapshot",network_snapshot_id=SnapshotId(),
            running=false,state="error",reason,detail,updated_mono_ns=MonoNs(),updated_wall=Wall(),peers=Array.Empty<object>(),
            source="passive_udp_size_signature_estimate",algorithm="DS3ConnectionInfo_ETWPingMonitor_compatible",
            game_exe="DarkSoulsIII.exe",no_packets_sent=true};
        if(!OperatingSystem.IsWindows())
        {PersistFailure(Failure("unsupported_platform","ETW requires Windows"),log,latestPath,statusPath);return 4;}
        if(!(TraceEventSession.IsElevated()??false))
        {
            var fail=Failure("permission_denied","Kernel network ETW needs administrator privileges. No elevation attempted.");
            PersistFailure(fail,log,latestPath,statusPath);return 5;
        }
        var gate=new object();var estimator=new Estimator();
        var games=new Dictionary<int,long>();var steam=new HashSet<int>();var allowPeers=new HashSet<string>();
        long events=0,estimates=0;double latestEtwMs=0;var lastEtwMono=Stopwatch.GetTimestamp();
        var cancelled=false;string stopReason="unknown";
        using var raw=new StreamWriter(Path.Combine(output,"events.jsonl"),append:true){AutoFlush=true};
        void Log(StreamWriter stream,object value)=>stream.WriteLine(JsonSerializer.Serialize(value,JsonOptions));
        string SessionState()=>games.Count==0?"waiting_for_game":"capturing";
        void UpdateProcesses()
        {
            var found=new Dictionary<int,long>();
            foreach(var process in Process.GetProcessesByName("DarkSoulsIII"))using(process)
            {
                try{if((fixedPid==0||process.Id==fixedPid)&&!process.HasExited)found[process.Id]=process.StartTime.ToUniversalTime().Ticks;}
                catch(InvalidOperationException){}catch(System.ComponentModel.Win32Exception){}
            }
            var steamPids=new HashSet<int>();
            if(steamOptIn)foreach(var process in Process.GetProcessesByName("steam"))using(process){steamPids.Add(process.Id);}
            var mapped=new HashSet<string>();
            if(peerFile!=null&&File.Exists(peerFile))try
            {
                using var document=JsonDocument.Parse(File.ReadAllText(peerFile));
                foreach(var item in document.RootElement.GetProperty("peers").EnumerateArray())
                {
                    var address=item.GetProperty("address").GetString();var port=item.GetProperty("port").GetInt32();
                    if(IPAddress.TryParse(address,out var ip)&&port>0&&port<=65535)mapped.Add(ip+":"+port);
                }
            }
            catch(Exception e)when(e is JsonException or IOException or KeyNotFoundException or InvalidOperationException){}
            lock(gate)
            {
                if(!games.OrderBy(x=>x.Key).SequenceEqual(found.OrderBy(x=>x.Key)))
                {
                    Log(raw,new {type="game_process_scope",wall=Wall(),mono_ns=MonoNs(),pids=found.Keys,
                        game_exe="DarkSoulsIII.exe",generation_changed=true});
                    // A process restart invalidates old timings and endpoint associations.
                    estimator=new Estimator();allowPeers.Clear();
                }
                games=found;steam=steamPids;allowPeers.UnionWith(mapped);
                estimator.RemoveProcesses(new HashSet<int>(games.Keys.Concat(steam)));
            }
        }
        try
        {
            using var session=new TraceEventSession(sessionName){StopOnDispose=true};
            void Observe(UdpIpTraceData packet,bool send)
            {
                if(packet.size!=(send?Estimator.SendSize:Estimator.ReceiveSize))return;
                lock(gate)
                {
                    var remote=(send?packet.daddr:packet.saddr).ToString();
                    var port=send?packet.dport:packet.sport;
                    var local=(send?packet.saddr:packet.daddr).ToString();
                    var localPort=send?packet.sport:packet.dport;
                    var endpoint=remote+":"+port;
                    long generation;string scope;
                    if(games.TryGetValue(packet.ProcessID,out generation))
                    {scope="DarkSoulsIII.exe";allowPeers.Add(endpoint);}
                    else if(games.Count>0&&steamOptIn&&steam.Contains(packet.ProcessID)&&allowPeers.Contains(endpoint))
                    {scope="Steam.exe_endpoint_explicitly_associated";generation=games.Values.Max();}
                    else return;
                    latestEtwMs=packet.TimeStampRelativeMSec;lastEtwMono=Stopwatch.GetTimestamp();events++;
                    var peer=new PeerKey(packet.ProcessID,generation,local,localPort,remote,port,scope);
                    var observed=new Observation(peer,packet.TimeStampRelativeMSec,send,packet.size);
                    Log(raw,new {type="udp_size_signature",mono_ns=MonoNs(),wall=Wall(),event_wall=packet.TimeStamp.ToUniversalTime().ToString("O"),
                        etw_relative_ms=observed.EventMs,peer,direction=send?"send":"receive",size=packet.size,
                        source="passive_udp_size_signature_estimate",peer_actor_mapping="unresolved",
                        timestamp_note="mono_ns/wall are processing time; RTT uses ETW event timestamps"});
                    var estimate=estimator.Observe(observed);
                    if(estimate is { } e)
                    {
                        estimates++;
                        Log(log,new {type="rtt_candidate",mono_ns=MonoNs(),wall=Wall(),event_wall=packet.TimeStamp.ToUniversalTime().ToString("O"),
                            etw_relative_ms=e.EventMs,endpoint_id=e.Peer.EndpointId,e.Peer,rtt_estimate_ms=e.RttMs,e.AverageMs,e.JitterMs,e.Samples,
                            e.AmbiguousPendingSends,e.MatchQuality,source="passive_udp_size_signature_estimate",
                            algorithm="DS3ConnectionInfo_ETWPingMonitor_compatible",
                            valid=true,validity="recent_heuristic_estimate",expires_after_ms=Estimator.ExpiryMs,
                            transaction_id_verified=false,one_way_delay_available=false,assignment="unknown"});
                    }
                }
            }
            UpdateProcesses();
            session.EnableKernelProvider(KernelTraceEventParser.Keywords.NetworkTCPIP);
            session.Source.Kernel.UdpIpSend+=packet=>Observe(packet,true);
            session.Source.Kernel.UdpIpRecv+=packet=>Observe(packet,false);
            Log(raw,new {type="started",wall=startedWall,mono_ns=MonoNs(),session=sessionName,hours,stop_file=stop,
                game_exe="DarkSoulsIII.exe",steam_known_peers_only=steamOptIn,source="passive_udp_size_signature_estimate",
                payloads_captured=false,no_packets_sent=true});
            var processing=Task.Run(()=>session.Source.Process());
            Console.CancelKeyPress+=(_,e)=>{e.Cancel=true;cancelled=true;};
            while(!cancelled)
            {
                if(File.Exists(stop)){stopReason="stop_file";break;}
                if(Stopwatch.GetElapsedTime(started).TotalHours>=hours){stopReason="duration_limit";break;}
                if(processing.IsCompleted){stopReason=processing.IsFaulted?"etw_processing_error":"etw_ended";break;}
                UpdateProcesses();
                lock(gate)
                {
                    var nowMs=latestEtwMs+Stopwatch.GetElapsedTime(lastEtwMono).TotalMilliseconds;
                    estimator.ExpireUnused(nowMs);
                    WriteStatus(new {type="network_snapshot",network_snapshot_id=SnapshotId(),
                        running=true,state=SessionState(),updated_wall=Wall(),updated_mono_ns=MonoNs(),session=sessionName,
                        elapsed_seconds=Stopwatch.GetElapsedTime(started).TotalSeconds,max_hours=hours,game_pids=games.Keys,
                        signature_events=events,rtt_candidates=estimates,peers=estimator.Readings(nowMs),
                        source="passive_udp_size_signature_estimate",algorithm="DS3ConnectionInfo_ETWPingMonitor_compatible",
                        game_exe="DarkSoulsIII.exe",
                        steam_known_peers_only=steamOptIn,stop_file=stop,no_packets_sent=true});
                }
                Thread.Sleep(500);
            }
            if(cancelled)stopReason="console_cancel";
            session.Stop();processing.Wait(TimeSpan.FromSeconds(5));
            WriteStatus(new {type="network_snapshot",network_snapshot_id=SnapshotId(),running=false,state="stopped",reason=stopReason,
                updated_wall=Wall(),updated_mono_ns=MonoNs(),peers=Array.Empty<object>(),
                elapsed_seconds=Stopwatch.GetElapsedTime(started).TotalSeconds,signature_events=events,rtt_candidates=estimates,
                source="passive_udp_size_signature_estimate",algorithm="DS3ConnectionInfo_ETWPingMonitor_compatible"});
            return processing.IsFaulted?6:0;
        }
        catch(Exception e)
        {
            var fail=Failure("etw_start_or_processing_failed",e.ToString());
            PersistFailure(fail,log,latestPath,statusPath);return 6;
        }
    }

    static int SelfTestIo()
    {
        var folder=Path.Combine(AppContext.BaseDirectory,"io_self_test_"+Environment.ProcessId);
        Directory.CreateDirectory(folder);
        var path=Path.Combine(folder,"latest.json");
        AtomicJson(path,new{value=1});
        var reader=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.ReadWrite);
        var writing=Task.Run(()=>AtomicJson(path,new{value=2}));
        Thread.Sleep(150);
        if(writing.IsCompleted)throw new Exception("Expected Windows sharing conflict before releasing reader");
        reader.Dispose();
        if(!writing.Wait(3000)||!File.ReadAllText(path).Contains("2"))throw new Exception("Retry did not recover");
        using var blocked=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.ReadWrite);
        var failed=false;
        try{AtomicJson(path,new{value=3});}
        catch(Exception e)when(e is IOException or UnauthorizedAccessException){failed=true;}
        if(!failed)throw new Exception("Permanent lock was not reported");
        using var journal=new StreamWriter(Path.Combine(folder,"network.jsonl")){AutoFlush=true};
        PersistFailure(new{running=false,state="error",reason="synthetic_permanent_lock"},journal,path,Path.Combine(folder,"status.json"));
        journal.Dispose();
        if(!File.ReadAllText(Path.Combine(folder,"network.jsonl")).Contains("synthetic_permanent_lock"))
            throw new Exception("Permanent failure was not journaled");
        if(!File.ReadAllText(Path.Combine(folder,"status.json")).Contains("synthetic_permanent_lock"))
            throw new Exception("Independent status path was not attempted");
        Console.WriteLine(JsonSerializer.Serialize(new{self_test_io=true,passed=true,checks=4,network_observation=false}));
        return 0;
    }

    static int SelfTest()
    {
        var count=0;
        void Check(bool ok,string label){count++;if(!ok)throw new Exception("FAIL: "+label);}
        var peer=new PeerKey(42,1,"127.0.0.1",1234,"192.0.2.1",5678,"synthetic");
        var estimator=new Estimator();
        Check(estimator.Observe(new(peer,1,false,68))==null,"orphan reply");
        Check(estimator.Observe(new(peer,10,true,55))==null&&estimator.Count==0,"wrong size");
        Check(estimator.Observe(new(peer,100,true,56))==null,"send pending");
        var first=estimator.Observe(new(peer,123,false,68));
        Check(first?.RttMs==23&&first?.Samples==1,"23ms estimate");
        Check(estimator.Observe(new(peer,124,false,68))==null,"duplicate reply");
        estimator.Observe(new(peer,200,true,56));
        Check(estimator.Observe(new(peer with{LocalPort=1235},223,false,68))==null,"local port mismatch");
        Check(estimator.Observe(new(peer with{ProcessId=43},223,false,68))==null,"pid mismatch");
        Check(estimator.Observe(new(peer with{Generation=2},223,false,68))==null,"generation mismatch");
        Check(estimator.Observe(new(peer,199,false,68))==null,"backward timestamp");
        var second=estimator.Observe(new(peer,227,false,68));
        Check(second?.RttMs==27&&second?.AverageMs==25&&second?.JitterMs==2,"average jitter");
        estimator.Observe(new(peer,300,true,56));estimator.Observe(new(peer,310,true,56));
        var ambiguous=estimator.Observe(new(peer,340,false,68));
        Check(ambiguous?.RttMs==30&&ambiguous?.AmbiguousPendingSends==1,"warmup latest ambiguous send");
        estimator.Observe(new(peer,20000,true,56));estimator.Observe(new(peer,20010,true,56));
        Check(estimator.Observe(new(peer,20040,false,68))?.RttMs==40,"after warmup preserve oldest send");
        estimator.Observe(new(peer,30000,true,56));
        Check(estimator.Observe(new(peer,40001,false,68))==null,"late response rejected");
        Check(JsonSerializer.Serialize(estimator.Readings(50000),JsonOptions).Contains("expired"),"expiry");
        estimator.RemoveProcesses(new HashSet<int>{43});Check(estimator.Count==0,"process removal clears rows");
        Console.WriteLine(JsonSerializer.Serialize(new {self_test=true,passed=true,checks=count,network_observation=false}));
        return 0;
    }
}
