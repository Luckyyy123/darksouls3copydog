"""Show parry evidence for the run in the most recent capture heartbeat."""
import json
from collections import Counter, defaultdict
from pathlib import Path

from auto_trace import DIAGNOSTIC_FORCE_SOURCE, DIAGNOSTIC_GATES, DIAGNOSTIC_UNATTRIBUTED_SOURCE, decode_parry_diagnostic

UNKNOWN_LABELS = ('未细分', '其他', '姿态缺失或过期', '缺少有效攻击形体',
                  '超出可信预测时长', '转角超过上限', '计算预算用尽', '连续碰撞计算未能确定')
GATE_LABELS = ('开关与 Shift', '目标进入攻击', '能读取原生动画', '自身状态',
               '本地输入', '装备模型', '距离', '刀弧接触', '出手时窗', '提交前状态',
               '防御请求状态', '自身脱离受击硬直', '同一攻击首次请求', '输入队列调用')
ATTEMPT_LABELS = {'blocked_hitstun':'自身受击硬直', 'unreadable_self':'提交前快照无法复核',
                  'duplicate':'同一攻击重复', 'input_disabled':'输入未开启',
                  'queue_failed':'提交路径返回失败', 'submitted':'记录为已提交',
                  'not_parryable':'不属于可弹攻击', 'waiting_timing':'等待出手时机',
                  'timing_missed':'本刀时窗已过', 'timing_unknown':'缺少可靠时机数据', 'unknown':'未知结果'}


def journal(path):
    with path.open(encoding='utf-8-sig') as stream:
        for line in stream:
            if not line.endswith('\n'):
                break
            yield json.loads(line)


def request_key(event):
    return (event['self'], event['target'], event['submitted_tick32'], event.get('defense_requests'),
            event.get('attack_animation'), event.get('tool'), event['source'] & (DIAGNOSTIC_FORCE_SOURCE | DIAGNOSTIC_UNATTRIBUTED_SOURCE))


def current_run(folder, status):
    trace = status.get('automatic_trace') or {}
    pid, build, run = status.get('game_pid'), status.get('copydog_build_id'), trace.get('session_id')
    if not status.get('running') or not pid or not build or run is None:
        return None
    cutoff = status['updated']
    cursor = trace['last_sequence']
    first_ms = None
    last_sequence = 0
    counts = Counter()
    requests, candidates = {}, defaultdict(list)
    queue_returns, confirmed = {}, {}
    forced_episodes = Counter()
    for event in journal(folder/'auto_trace.jsonl'):
        if (event.get('game_pid'), event.get('build_id'), event.get('session_id')) != (pid, build, run):
            continue
        if event['wall'] > cutoff:
            break
        if event['kind'] == 'auto_trace_session' and first_ms is None:
            first_ms = event['t_ms']
        if first_ms is None or event['kind'] != 'auto_trace':
            continue
        sequence = event['sequence']
        if sequence > cursor:
            break
        if sequence <= last_sequence:
            continue
        last_sequence = sequence
        counts['records'] += 1
        kind = event['event_kind']
        if kind == 15 and event.get('diagnostic_temporal_decision'):
            counts['scheduler_records'] += 1
            counts['scheduler_' + event['diagnostic_temporal_decision']] += 1
        if kind == 2 and event['source'] & 1024:
            counts['plans'] += 1
            counts['state_skips'] += event['reason'] == 5
            counts['contact_plans'] += bool(event['source'] & 256)
            counts['unconfirmed_contacts'] += event['reason'] == 16
            if event['source'] & 1048576:
                counts[UNKNOWN_LABELS[(event['source'] >> 21) & 7]] += 1
        elif kind == 3:
            if event['defense_kind'] == 2:
                requests[sequence] = event
                counts['contact_requests'] += bool(event['source'] & 256)
                if event['source'] & DIAGNOSTIC_FORCE_SOURCE:
                    counts['forced_requests'] += 1
                    counts['forced_animation_unattributed'] += bool(event['source'] & DIAGNOSTIC_UNATTRIBUTED_SOURCE)
                    episode = tuple(event.get(k) for k in ('self', 'target', 'target_network_id',
                                                           'attack_animation', 'attack_started_tick32'))
                    if None in episode:
                        counts['forced_episode_unknown'] += 1
                    else:
                        forced_episodes[episode] += 1
                else:
                    counts['ordinary_contact_requests'] += bool(event['source'] & 256)
            elif event['defense_kind'] == 1:
                counts['rolls'] += 1
        elif kind == 4 and event['defense_kind'] == 2:
            queue_returns[request_key(event)] = event
        elif kind == 5 and event['defense_kind'] == 2 and not event['source'] & DIAGNOSTIC_UNATTRIBUTED_SOURCE:
            confirmed[request_key(event)] = event
        elif kind in (15, 16):
            # A capture already running when the DLL changed may have written "unknown"
            # as the event name. The original numeric event kind and ABI words survive.
            decode_parry_diagnostic(event)
            if kind == 15:
                counts['diagnostic_gate_snapshots'] += 1
                counts['original_' + event['diagnostic_original_decision']] += 1
                for gate, state in event['diagnostic_gate_states'].items():
                    if gate != 'queue':
                        counts[f'diagnostic_gate_{gate}_{state}'] += 1
            else:
                counts['diagnostic_attempts'] += 1
                counts['attempt_' + event['diagnostic_attempt_result']] += 1
                counts['diagnostic_gate_queue_' + event['diagnostic_gate_states']['queue']] += 1
    if first_ms is None:
        return None

    request_keys = Counter(request_key(req) for req in requests.values())

    def paired(req, records):
        key = request_key(req)
        event = records.get(key)
        if (key[2] is not None and request_keys[key] == 1 and event is not None
                and event['sequence'] > req['sequence']):
            return event
        return None

    for req in requests.values():
        accepted = paired(req, queue_returns) is not None
        saw_animation = paired(req, confirmed) is not None
        counts['queue_returns'] += accepted
        counts['confirmed_requests'] += saw_animation
        if req['source'] & DIAGNOSTIC_FORCE_SOURCE:
            counts['forced_queue_returns'] += accepted
            counts['forced_confirmed_requests'] += saw_animation

    for row in journal(folder/'events.jsonl'):
        if row.get('kind') != 'trial_result' or row['parry_t_ms'] < first_ms or row['wall_logged'] > cutoff:
            continue
        if row.get('start_copydog_build_id') != build:
            continue
        if row.get('game_pid', pid) != pid or row.get('trace_session_id', run) != run:
            continue
        counts['trials'] += 1
        counts['strict_success'] += row['label'] == 'success'
        # Older monitors also labelled counter increments in an already open trial.
        # A counter alone has no submit sequence and cannot identify the action.
        link = row.get('automatic_trace_submission') or {}
        req = requests.get(link.get('sequence'))
        if (not req or link.get('session_id') != run or link.get('tick32') != req['tick32'] or
                (link.get('self'), link.get('target')) != (req['self'], req['target']) or
                row.get('self_actor') != req['self']):
            continue
        confirmation = paired(req, confirmed)
        if (confirmation and confirmation['self_animation'] == row['parry_animation'] and
                0 <= row['parry_t_ms'] - req['t_ms'] <= 400):
            candidates[req['sequence']].append((row['label'], row.get('hp_loss', 0)))
    counts['requests'] = len(requests)
    counts['forced_episodes'] = len(forced_episodes)
    counts['repeated_forced_requests'] = sum(n - 1 for n in forced_episodes.values())
    counts['matched_trials'] = sum(len(labels) == 1 for labels in candidates.values())
    counts['matched_success'] = sum(len(results) == 1 and results[0][0] == 'success' for results in candidates.values())
    counts['ambiguous_requests'] = sum(len(labels) > 1 for labels in candidates.values())
    counts['missing_records'] = max(0, cursor - counts['records'])
    recent = []
    for seq in sorted(requests, reverse=True)[:6]:
        req, matches = requests[seq], candidates.get(seq, [])
        result = '尚无唯一配对的结算'
        if len(matches) == 1:
            label, hp_loss = matches[0]
            result = {'success':'同期无伤正面处决', 'failed':'同期未达无伤处决',
                      'incomplete':'同期记录不完整'}.get(label, label)
            if hp_loss:
                result += f'，掉血 {hp_loss}'
        if req['source'] & DIAGNOSTIC_FORCE_SOURCE:
            result = '诊断强制请求，' + result
            if req['source'] & DIAGNOSTIC_UNATTRIBUTED_SOURCE:
                result += '，同期动画归属未确定'
        recent.append((req['wall'], req.get('distance_mm'), bool(req['source'] & 256), result))
    return counts, recent


def diagnostic_lines(counts):
    if not (counts['diagnostic_gate_snapshots'] or counts['diagnostic_attempts'] or counts['forced_requests']):
        return []
    lines = [
        f"诊断门快照 {counts['diagnostic_gate_snapshots']}。原算法选择跳过 {counts['original_skip']}，"
        f"等一帧 {counts['original_wait']}，现在弹 {counts['original_press']}，未形成计划 {counts['original_no_plan']}。",
    ]
    if counts['scheduler_records']:
        lines = [f"统一弹反 CPU 时间判定快照 {counts['scheduler_records']}："
                 f"跳过 {counts['scheduler_skip']}，等待 {counts['scheduler_wait']}，"
                 f"现在弹 {counts['scheduler_press']}。旧求解器不参与自动提交。"]
    if counts['original_unknown']:
        lines.append(f"无法识别的原判定 {counts['original_unknown']}。")
    for gate, label in zip(DIAGNOSTIC_GATES, GATE_LABELS):
        passed, blocked, untested, invalid = (counts[f'diagnostic_gate_{gate}_{state}']
                                             for state in ('passed', 'blocked', 'not_evaluated', 'inconsistent'))
        if gate == 'queue':
            label += '（按诊断尝试结果）'
        line = f"  {label}　通过 {passed}，拦截 {blocked}，未评估 {untested}"
        if invalid:
            line += f"，标记不一致 {invalid}"
        lines.append(line)
    outcomes = [f"{label} {counts['attempt_' + key]}" for key, label in ATTEMPT_LABELS.items() if counts['attempt_' + key]]
    result = '，'.join(outcomes) if outcomes else '尚无结果记录'
    lines.append(f"诊断尝试结果 {counts['diagnostic_attempts']} 次。{result}。")
    lines.append(f"带强制标记的 DLL 提交尝试 {counts['forced_requests']}，其中动作调用已返回 {counts['forced_queue_returns']}，"
                 f"观察到可配对的自身弹反动画 {counts['forced_confirmed_requests']}，"
                 f"前一请求尚未确认、同期动画归属未定 {counts['forced_animation_unattributed']}。判断是否弹反成功仍需战斗结算。")
    if counts['forced_requests']:
        lines.append(f"能定位的不同攻击 {counts['forced_episodes']}，同一次攻击的额外强制提交 {counts['repeated_forced_requests']}，"
                     f"缺少攻击标识的强制提交 {counts['forced_episode_unknown']}。")
    return lines


def diagnostic_mode_line(status, counts):
    flag = status.get('diagnostic_parry_enabled')
    if flag is None:
        if counts and (counts['diagnostic_gate_snapshots'] or counts['diagnostic_attempts'] or counts['forced_requests']):
            return '本轮已记录强制弹反诊断事件，本次心跳没有保存可确认的开关值。'
        if 'CopydogDiagnosticParryEnabled' in (status.get('copydog_symbols_available') or ()):
            return '已发现强制弹反诊断开关，本次未读到开关值。'
        return None
    if flag:
        if str(status.get('copydog_build_id', '')).startswith('SC23-'):
            return 'SC23 统一弹反已开启，Shift 控制判定；自动翻滚和背刺已解除诊断总屏蔽，沿用 F7 和原动作条件。'
        return '强制弹反诊断已开启，由 Shift 控制出手，其他自动出手已屏蔽。'
    if str(status.get('copydog_build_id', '')).startswith('SC23-'):
        return 'SC23 自动弹反已关闭，旧算法不会回切；翻滚和背刺沿用原条件。'
    return '强制弹反诊断已关闭。'


def game_status_line(status):
    pid = status.get('game_pid')
    state = status.get('state')
    if pid is not None:
        suffix = {'read_access_unavailable': '，暂时无法读取遥测',
                  'waiting_for_game_module': '，正在等待游戏主模块'}.get(state, '')
        return f'游戏已启动，PID {pid}{suffix}。'
    if state == 'waiting_ambiguous_multiple_game_processes':
        return '已发现多个游戏进程，等待唯一 PID。'
    if status.get('game_running') is False:
        return '尚未发现游戏进程。'
    if state == 'stopped':
        return '采集已结束，游戏进程状态未更新。'
    if 'game_running' not in status:
        return '这份旧记录没有单独保存游戏进程状态。'
    return '本次状态记录没有可确认的游戏 PID。'


def main():
    active = Path(__file__).resolve().with_name('active_session.json')
    if not active.exists():
        print('尚无监测会话记录。')
        return
    folder = Path(json.loads(active.read_text(encoding='utf-8-sig'))['folder'])
    status = json.loads((folder/'latest.json').read_text(encoding='utf-8-sig'))
    print(f"最近更新 {status['updated']}　状态 {status['state']}　采集 PID {status['monitor_pid']}")
    print(game_status_line(status))
    if status.get('game_pid') is None or status.get('telemetry_attached') is False:
        return
    print(f"驻留构建 {status.get('copydog_build_id')}　读取错误 {status.get('read_errors')}")
    report = current_run(folder, status)
    mode = diagnostic_mode_line(status, report[0] if report else None)
    if mode:
        print(mode)
    if report is None:
        print('本次游戏运行的 DLL 事件尚不可用。')
        return
    counts, recent = report
    print(f"CPU 逐帧计划 {counts['plans']}，状态门拦下 {counts['state_skips']}，刀弧接触候选 {counts['contact_plans']}。")
    if counts['unconfirmed_contacts']:
        print(f"有 {counts['unconfirmed_contacts']} 次到了出手时间，原算法因缺少刀弧接触证据选择了等待或跳过。")
    unknown = [f"{label} {counts[label]}" for label in UNKNOWN_LABELS if counts[label]]
    if unknown:
        print('未能确认接触的逐帧记录　' + '，'.join(unknown))
    print(f"DLL 弹反提交尝试 {counts['requests']}，其中常规路径带刀弧接触预测 {counts['ordinary_contact_requests']}。"
          f"观察到相符的自身弹反动画 {counts['confirmed_requests']}。另有翻滚请求 {counts['rolls']}。")
    for line in diagnostic_lines(counts):
        print(line)
    print(f"已结算的自身弹反动作 {counts['trials']}，其中无伤正面处决 {counts['strict_success']}。"
          f"能唯一配对到 DLL 请求的动作 {counts['matched_trials']}，其中无伤正面处决 {counts['matched_success']}。")
    if counts['ambiguous_requests'] or counts['missing_records']:
        print(f"配对有歧义的请求 {counts['ambiguous_requests']}，未采到的 DLL 事件 {counts['missing_records']}。")
    for recorded, distance, contact, outcome in recent:
        dist = f'{distance / 1000:.2f}' if distance is not None else '未记录'
        evidence = '有刀弧接触预测' if contact else '无刀弧接触证据'
        print(f"{recorded[11:23]}　提交距离 {dist}　{evidence}　{outcome}")
    print('手动操作可能与 DLL 请求重合。现有记录无法单独确定操作者。')


if __name__ == '__main__':
    main()
