using System.Collections.Generic;

namespace Ds3Latency;

public readonly record struct PeerKey(int ProcessId,long Generation,string LocalAddress,int LocalPort,
    string RemoteAddress,int RemotePort,string ProcessScope)
{
    public string EndpointId=>$"{ProcessId}/{Generation}/{LocalAddress}:{LocalPort}->{RemoteAddress}:{RemotePort}";
}
public readonly record struct Observation(PeerKey Peer,double EventMs,bool Send,int Size);
public readonly record struct Estimate(PeerKey Peer,double EventMs,double RttMs,int Samples,
    double AverageMs,double JitterMs,int AmbiguousPendingSends,string MatchQuality);

public sealed class Estimator
{
    public const int SendSize=56,ReceiveSize=68;
    public const double WarmupMs=10000,MaxRttMs=10000,ExpiryMs=15000;
    private sealed class Row
    {
        public double FirstSend=-1,PendingSend=-1,LastReply=-1,Latest=-1;
        public int Ambiguous=0,LatestAmbiguous=0,Samples=0;
        public Queue<double> Values=new();
    }
    private readonly Dictionary<PeerKey,Row> rows=new();
    public int Count=>rows.Count;

    public Estimate? Observe(Observation e)
    {
        if (!double.IsFinite(e.EventMs)||e.EventMs<0||e.Size!=(e.Send?SendSize:ReceiveSize))return null;
        if (!rows.TryGetValue(e.Peer,out var row))
        {
            if(!e.Send||rows.Count>=256)return null;
            row=new Row();rows.Add(e.Peer,row);
        }
        if(e.Send)
        {
            if(row.FirstSend<0)row.FirstSend=e.EventMs;
            if(row.PendingSend>=0&&e.EventMs<row.PendingSend)return null;
            if(row.PendingSend<0||e.EventMs-row.PendingSend>MaxRttMs)
            {row.PendingSend=e.EventMs;row.Ambiguous=0;}
            else
            {
                row.Ambiguous++;
                if(e.EventMs-row.FirstSend<WarmupMs)row.PendingSend=e.EventMs;
            }
            return null;
        }
        if(row.PendingSend<0||e.EventMs<row.PendingSend)return null;
        var delta=e.EventMs-row.PendingSend;row.PendingSend=-1;
        if(delta>MaxRttMs)return null;
        row.Latest=delta;row.LastReply=e.EventMs;row.LatestAmbiguous=row.Ambiguous;row.Samples++;
        row.Values.Enqueue(delta);if(row.Values.Count>10)row.Values.Dequeue();
        var avg=row.Values.Average();
        var jitter=Math.Sqrt(row.Values.Select(x=>(x-avg)*(x-avg)).Average());
        return new(e.Peer,e.EventMs,delta,row.Samples,avg,jitter,row.Ambiguous,
            row.Ambiguous==0?"size_and_endpoint_pair_only":"ambiguous_multiple_sends");
    }

    public object[] Readings(double nowMs)
    {
        return rows.Where(x=>x.Value.Samples>0).Select(x=>
        {
            var r=x.Value;var age=Math.Max(0,nowMs-r.LastReply);
            var avg=r.Values.Average();
            return (object)new {endpoint_id=x.Key.EndpointId,peer=x.Key,rtt_estimate_ms=r.Latest,average_ms=avg,
                jitter_ms=Math.Sqrt(r.Values.Select(v=>(v-avg)*(v-avg)).Average()),samples=r.Samples,
                age_ms=age,valid=age<=ExpiryMs,validity=age<=ExpiryMs?"recent_heuristic_estimate":"expired",
                expires_after_ms=ExpiryMs,ambiguous_pending_sends=r.LatestAmbiguous,
                match_quality=r.LatestAmbiguous==0?"size_and_endpoint_pair_only":"ambiguous_multiple_sends",
                source="passive_udp_size_signature_estimate",algorithm="DS3ConnectionInfo_ETWPingMonitor_compatible",
                assignment="unknown",
                transaction_id_verified=false,one_way_delay_available=false};
        }).ToArray();
    }

    public void RemoveProcesses(ISet<int> allowed)
    {foreach(var k in rows.Keys.Where(k=>!allowed.Contains(k.ProcessId)).ToArray())rows.Remove(k);}

    public void ExpireUnused(double nowMs)
    {foreach(var k in rows.Where(x=>nowMs-Math.Max(x.Value.PendingSend,x.Value.LastReply)>60000).Select(x=>x.Key).ToArray())rows.Remove(k);}
}
