"""Windows process discovery and read-only DS3 snapshots. No target execution."""
import ctypes as C
from ctypes import wintypes as W
import math
import struct
from auto_trace import resident_exports
from native_pose import read_clock

K=C.WinDLL('kernel32',use_last_error=True)
U=C.WinDLL('user32',use_last_error=True)
HANDLE=C.c_void_p
INVALID_HANDLE=C.c_void_p(-1).value


class ProcessEntry(C.Structure):
    _fields_=[('dwSize',W.DWORD),('cntUsage',W.DWORD),('pid',W.DWORD),('heap',C.c_size_t),
              ('moduleId',W.DWORD),('threads',W.DWORD),('parentPid',W.DWORD),
              ('priority',W.LONG),('flags',W.DWORD),('name',W.WCHAR*260)]


class ModuleEntry(C.Structure):
    _fields_=[('dwSize',W.DWORD),('moduleId',W.DWORD),('pid',W.DWORD),('globalUsage',W.DWORD),
              ('processUsage',W.DWORD),('base',C.c_void_p),('size',W.DWORD),('module',HANDLE),
              ('name',W.WCHAR*256),('path',W.WCHAR*260)]


K.CreateToolhelp32Snapshot.argtypes=[W.DWORD,W.DWORD]
K.CreateToolhelp32Snapshot.restype=HANDLE
K.Process32FirstW.argtypes=[HANDLE,C.POINTER(ProcessEntry)]
K.Process32NextW.argtypes=[HANDLE,C.POINTER(ProcessEntry)]
K.Module32FirstW.argtypes=[HANDLE,C.POINTER(ModuleEntry)]
K.Module32NextW.argtypes=[HANDLE,C.POINTER(ModuleEntry)]
K.CloseHandle.argtypes=[HANDLE]
K.OpenProcess.argtypes=[W.DWORD,W.BOOL,W.DWORD]
K.OpenProcess.restype=HANDLE
K.ReadProcessMemory.argtypes=[HANDLE,C.c_void_p,C.c_void_p,C.c_size_t,C.POINTER(C.c_size_t)]
K.WaitForSingleObject.argtypes=[HANDLE,W.DWORD]
K.WaitForSingleObject.restype=W.DWORD
SYNCHRONIZE=0x100000
QUERY_LIMITED_INFORMATION=0x1000
PROCESS_VM_READ=0x10
WAIT_TIMEOUT=258
ERROR_NO_MORE_FILES=18
GAME_EXE_NAME='darksoulsiii.exe'
K.GetTickCount64.restype=C.c_ulonglong
U.GetForegroundWindow.restype=HANDLE
U.GetWindowThreadProcessId.argtypes=[HANDLE,C.POINTER(W.DWORD)]
U.GetAsyncKeyState.argtypes=[C.c_int]
U.GetAsyncKeyState.restype=C.c_short


def game_pids(*,require_snapshot=False):
    result=[]
    snapshot=K.CreateToolhelp32Snapshot(2,0)
    if snapshot in (None,INVALID_HANDLE):
        if require_snapshot:
            raise OSError(C.get_last_error(),'Cannot obtain process snapshot')
        return result
    try:
        item=ProcessEntry();item.dwSize=C.sizeof(item)
        ok=K.Process32FirstW(snapshot,C.byref(item))
        while ok:
            if item.threads and item.name.casefold()==GAME_EXE_NAME:
                handle=K.OpenProcess(SYNCHRONIZE,False,item.pid)
                try:
                    if not handle or K.WaitForSingleObject(handle,0)==WAIT_TIMEOUT:
                        result.append(item.pid)
                finally:
                    if handle:
                        K.CloseHandle(handle)
            ok=K.Process32NextW(snapshot,C.byref(item))
        if require_snapshot:
            error=C.get_last_error()
            if error not in (0,ERROR_NO_MORE_FILES):
                raise OSError(error,'Cannot finish enumerating the process snapshot')
    finally:
        K.CloseHandle(snapshot)
    return result


def modules(pid):
    result=[]
    snapshot=K.CreateToolhelp32Snapshot(0x18,pid)
    if snapshot in (None,INVALID_HANDLE):
        return result
    try:
        item=ModuleEntry();item.dwSize=C.sizeof(item)
        ok=K.Module32FirstW(snapshot,C.byref(item))
        while ok:
            result.append(dict(name=item.name,path=item.path,base=item.base,size=item.size))
            ok=K.Module32NextW(snapshot,C.byref(item))
    finally:
        K.CloseHandle(snapshot)
    return result


def foreground_inputs(pid):
    foreground=W.DWORD()
    U.GetWindowThreadProcessId(U.GetForegroundWindow(),C.byref(foreground))
    focused=foreground.value==pid
    result=dict(game_focused=focused,
                mouse_right=bool(U.GetAsyncKeyState(2)<0) if focused else None,
                shift=bool(U.GetAsyncKeyState(16)<0) if focused else None,
                note='Task-specific input state, not proof of physical/manual origin')
    if focused and XINPUT is not None:
        controllers=[]
        for index in range(4):
            state=XInputState()
            if XINPUT.XInputGetState(index,C.byref(state))==0:
                controllers.append(dict(index=index,left_trigger=state.pad.lt,right_trigger=state.pad.rt,
                                        left_shoulder=bool(state.pad.buttons&0x100),right_shoulder=bool(state.pad.buttons&0x200)))
        result['controllers']=controllers
    return result


def find_game_reader():
    candidates=game_pids(require_snapshot=True)
    if len(candidates)!=1:
        return None,None,'waiting_ambiguous_multiple_game_processes' if candidates else 'waiting_for_game'
    info=dict(pid=candidates[0])
    try:
        reader=Reader(info['pid'])
    except OSError as ex:
        info['telemetry_error']=str(ex)
        return None,info,'read_access_unavailable'
    try:
        try:
            game=next((m for m in modules(reader.pid) if m['name'].casefold()==GAME_EXE_NAME),None)
        except OSError as ex:
            info['telemetry_error']=str(ex)
            game=None
        if not reader.alive():
            return None,None,'waiting_for_game'
        if game is None:
            return None,info,'waiting_for_game_module'
        result=reader,game,'waiting_for_character'
        reader=None
        return result
    finally:
        if reader:
            reader.close()


def game_process_status(reader,game,state):
    pid=reader.pid if reader else (game or {}).get('pid')
    if pid is not None or state=='waiting_ambiguous_multiple_game_processes':
        running=True
    else:
        running=False if state=='waiting_for_game' else None
    return dict(game_pid=pid,game_running=running,telemetry_attached=reader is not None)


class XInputPad(C.Structure):
    _fields_=[('buttons',W.WORD),('lt',W.BYTE),('rt',W.BYTE),('lx',C.c_short),('ly',C.c_short),('rx',C.c_short),('ry',C.c_short)]


class XInputState(C.Structure):
    _fields_=[('packet',W.DWORD),('pad',XInputPad)]


XINPUT=None
for _name in ('xinput1_4.dll','xinput9_1_0.dll'):
    try:
        XINPUT=C.WinDLL(_name)
        XINPUT.XInputGetState.argtypes=[W.DWORD,C.POINTER(XInputState)]
        XINPUT.XInputGetState.restype=W.DWORD
        break
    except OSError:
        pass


class Reader:
    def __init__(self,pid):
        self.pid=pid
        self.handle=K.OpenProcess(SYNCHRONIZE|QUERY_LIMITED_INFORMATION|PROCESS_VM_READ,False,pid)
        self.read_errors=0
        if not self.handle:
            raise OSError(C.get_last_error(),'Read-only OpenProcess failed')

    def alive(self):
        return bool(self.handle and K.WaitForSingleObject(self.handle,0)==WAIT_TIMEOUT)

    def close(self):
        if self.handle:
            K.CloseHandle(self.handle);self.handle=None

    def bytes(self,address,size):
        if not address or address<65536 or address+size>0x800000000000:
            return None
        buffer=C.create_string_buffer(size);done=C.c_size_t()
        if not K.ReadProcessMemory(self.handle,address,buffer,size,C.byref(done)) or done.value!=size:
            self.read_errors+=1
            return None
        return buffer.raw

    def value(self,address,fmt):
        raw=self.bytes(address,struct.calcsize(fmt))
        if raw is None:
            return None
        values=list(struct.unpack(fmt,raw))
        values=[None if isinstance(x,float) and not math.isfinite(x) else x for x in values]
        return values[0] if len(values)==1 else values

    def ptr(self,address):
        return self.value(address,'<Q') or 0

    def equipment(self,address):
        raw=self.bytes(address+0x2b8,0x8c) if address else None
        if raw is None:
            return None
        mode,left_slot,right_slot=struct.unpack_from('<3I',raw)
        weapons=list(struct.unpack_from('<6I',raw,0x74))
        left=weapons[left_slot*2] if left_slot<3 else None
        right=weapons[right_slot*2+1] if right_slot<3 else None
        return dict(hand_mode=mode,selected_slots=[left_slot,right_slot],weapon_slots_raw=weapons,
                    left_selected_raw=left,right_selected_raw=right,
                    active_tool_candidate_raw=left if mode in (1,2) else right if mode==3 else None)

    def actor(self,address,slot):
        if not address:
            return None
        module=self.ptr(address+0x1f90)
        roots=self.bytes(module,0x88) if module else None
        if roots is None:
            return None
        state=struct.unpack_from('<Q',roots,0)[0]
        stats=struct.unpack_from('<Q',roots,0x18)[0]
        physics=struct.unpack_from('<Q',roots,0x68)[0]
        animation=struct.unpack_from('<Q',roots,0x80)[0]
        vector=self.value(physics+0x70,'<7f') if physics else None
        hp=self.value(stats+0xd8,'<i') if stats else None
        flags=self.bytes(state+0x58,5) if state else None
        visible_animation=self.value(animation+0xc8,'<i') if animation else None
        native_clock=read_clock(self,module,visible_animation)
        return dict(slot=slot,actor=address,entity_id=self.value(address+0x78,'<i'),
                    actor_type=self.value(address+0x74,'<B'),
                    animation=visible_animation,
                    hp=hp,stamina=self.value(stats+0xf0,'<i') if stats else None,
                    position=vector[4:7] if vector else None,orientation=vector[:3] if vector else None,
                    flags58=flags[0] if flags else None,flags5c=flags[4] if flags else None,
                    readiness_raw=self.value(state+0x224,'<f') if state else None,
                    busy_raw=self.value(state+0x228,'<B') if state else None,
                    equipment=self.equipment(self.ptr(address+0x1fa0)),
                    native_animation_phase_ms=native_clock['phase_ms'] if native_clock else None,
                    native_animation_clock=native_clock,render_fps=None)


SYMBOLS={'Latency','LastAttackTime','ParryRequests','CopydogEnabled','AutoRollEnabled','CopydogDiagnosticParryEnabled',
         'CopyAction','ParryLast','Defense','Config','prevLockTarget',
         'CopydogPredictionSamples','CopydogPredictionQueries','CopydogPredictionContacts',
         'CopydogPredictionUnknown','CopydogPredictionClear','CopydogNativeParryRearms',
         'CopydogBulletDiagnostics','CopydogBulletLastId','CopydogBulletLastOwner',
         'CopydogDynamicParryCounters','CopydogAreaDiagnostics','CopydogLastAreaThreat'}


def telemetry_symbols(reader,module):
    """Resolve data exports from the loaded image; never execute target code."""
    return resident_exports(reader,module,SYMBOLS | {'CopydogAutoTrace','CopydogBuildId'})


def automatic_snapshot(reader,symbols):
    def get(name,fmt):
        return reader.value(symbols.get(name,0),fmt)
    result=dict(requests=get('ParryRequests','<Q'),enabled=get('CopydogEnabled','<i'),
                diagnostic_parry_enabled=get('CopydogDiagnosticParryEnabled','<i'),
                auto_roll_enabled=get('AutoRollEnabled','<i'),copy_action=get('CopyAction','<b'),
                lock_target=get('prevLockTarget','<Q'),latency_raw=get('Latency','<5i'),
                last_outgoing_hit_tick32=get('LastAttackTime','<5I'),
                tick32=int(K.GetTickCount64())&0xffffffff,
                parry_diagnostic_raw=get('ParryLast','<9Q'),
                layout_source='resident_PE_data_exports',manual_origin='unknown')
    result['geometry50ms']={key:get(name,'<Q') for key,name in (
        ('native_samples','CopydogPredictionSamples'),('melee_queries','CopydogPredictionQueries'),
        ('predicted_contacts','CopydogPredictionContacts'),('unknown_queries','CopydogPredictionUnknown'),
        ('clear_or_consumed_queries','CopydogPredictionClear'))}
    result['native_parry_rearms']=get('CopydogNativeParryRearms','<Q')
    dynamic=get('CopydogDynamicParryCounters','<8Q')
    result['dynamic_parry']=dict(zip(('native_observations','delay_samples','fallback',
        'plans','geometry_aligned','early','late','submitted'),dynamic)) if dynamic else None
    bullet=get('CopydogBulletDiagnostics','<9Q')
    result['bullet_diagnostics']=dict(zip(('callbacks','input_paused','owner_reject','invalid',
        'unknown_profile','geometry_queries','full_body_queries','predicted_contacts','published'),bullet)) if bullet else None
    area=get('CopydogAreaDiagnostics','<4Q')
    if area:
        result['ground_blast']=dict(zip(('estimated_floor','anchor_fuse','observed_area','consumed'),area))
        detail=get('CopydogLastAreaThreat','<Q')
        if detail:
            result['ground_blast'].update(last_id=detail>>32,eta_ms=detail&0xffff,
                source={1:'estimated_floor',2:'anchor_fuse',3:'observed_area',4:'observed_age_unknown'}.get((detail>>16)&0xffff))
    result['last_bullet_id']=get('CopydogBulletLastId','<i')
    result['last_bullet_owner_handle']=get('CopydogBulletLastOwner','<I')
    defense=get('Defense','<QQ11i3I3Q')
    if defense is not None:
        names=['self','target','phase','kind','tool','hp','previous_animation','target_animation','reason',
               'delay_tool','delay_kind','observed_dispatch_ms','have_delay','submitted_tick32','confirmed_tick32',
               'changed_tick32','requests','confirmations','cancellations']
        result['defense']=dict(zip(names,defense))
    config=get('Config','<18i')
    if config is not None:
        result['config_parry_ahead_ms']=config[0]
        result['config_parry_startup_ms']=config[17]
    return result
