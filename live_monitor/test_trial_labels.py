import unittest
from trial_labels import TrialTracker, load_parry_ids


PARRIES = {42032100,42036900,48032100,48036900}


def row(t, anim=0, hp=1000, others=None, actor=1, requests=None, target=None):
    return dict(t_ms=t,self=None if actor is None else dict(actor=actor,animation=anim,hp=hp),
                others=others if others is not None else [dict(slot=1,actor=2,animation=0,hp=1000)],
                automatic=dict(requests=requests),target_actor=target)


def peer(anim=0, actor=2, slot=1):
    return dict(slot=slot,actor=actor,animation=anim,hp=1000)


def traced(t, anim=0, *, pid=41, session=123, sequence=None, submitted=30, confirmed=None,
           requests=0, target_net=55, generation=1):
    sample=row(t, anim, requests=requests)
    sample['pid']=pid
    sample['self'].update(entity_id=11,generation=generation)
    sample['others'][0].update(entity_id=55,generation=3)
    trace=dict(state='ready',session_id=session)
    sample['automatic'].update(tick32=t & 0xffffffff,build_id='fixture',trace=trace)
    if sequence is not None:
        trace['recent_parry_submission']=dict(session_id=session,sequence=sequence,tick32=submitted & 0xffffffff,
            self=1,target=2,target_network_id=target_net,tool=900,self_mode=1,
            age_ms=(t-submitted)&0xffffffff)
        sample['automatic']['defense']=dict(kind=2,phase=1 if confirmed is None else 2,self=1,target=2,
            tool=900,previous_animation=anim,submitted_tick32=submitted & 0xffffffff,
            confirmed_tick32=0 if confirmed is None else confirmed & 0xffffffff)
    return sample


class TrialTests(unittest.TestCase):
    def setUp(self):
        self.tracker=TrialTracker(PARRIES)
        self.events=[]

    def send(self,*rows):
        for sample in rows:
            self.events.extend(self.tracker.update(sample))
        return [e for e in self.events if e["kind"]=="trial_result"]

    def begin(self,anim=48032100):
        self.send(row(0),row(10,anim))

    def test_success_requires_pair_and_grace(self):
        self.begin()
        self.send(row(100,48030201,others=[peer(70030)]))
        self.assertFalse([e for e in self.events if e["kind"]=="trial_result"])
        result=self.send(row(250,48030201,others=[peer(70030)]))[-1]
        self.assertEqual(result["label"],"success")
        self.assertEqual(result["automatic_status"],"unknown")

    def test_no_execution_fails(self):
        self.begin()
        self.assertEqual(self.send(row(3010))[-1]["reason"],"no_followup_frontal_execution")

    def test_self_hp_loss_fails(self):
        self.begin()
        self.assertEqual(self.send(row(90,48032100,hp=999))[-1]["reason"],"self_hp_loss")

    def test_hp_loss_at_start_uses_previous_sample(self):
        result=self.send(row(0),row(10,48032100,hp=999))[-1]
        self.assertEqual(result["reason"],"self_hp_loss_at_parry_start")

    def test_alias_36900_is_parry(self):
        for anim in (42036900,48036900):
            tracker=TrialTracker(PARRIES)
            tracker.update(row(0))
            events=tracker.update(row(10,anim))
            self.assertEqual(events[0]["kind"],"parry_animation_start")
            self.assertIsNotNone(tracker.active)

    def test_real_csv_includes_aliases(self):
        self.assertTrue(PARRIES.issubset(load_parry_ids()))

    def test_next_parry_ends_previous(self):
        self.begin()
        self.send(row(100))
        result=self.send(row(120,42036900))[-1]
        self.assertEqual(result["reason"],"next_parry_before_frontal_execution")
        self.assertEqual(self.tracker.active["trial_id"],2)

    def test_slot_replacement_cannot_be_execution_partner(self):
        self.begin()
        result=self.send(row(100,48030201,others=[peer(70030,actor=3)]),
                         row(600,48030201,others=[peer(70030,actor=3)]))[-1]
        self.assertEqual(result["label"],"incomplete")
        self.assertEqual(result["reason"],"target_identity_lost_or_replaced")

    def test_locked_target_must_match(self):
        self.send(row(0,others=[peer(),peer(actor=3,slot=2)]),
                  row(10,48032100,others=[peer(),peer(actor=3,slot=2)],target=2),
                  row(100,48030201,others=[peer(),peer(70030,actor=3,slot=2)]),
                  row(600,48030201,others=[peer(),peer(70030,actor=3,slot=2)]))
        result=[e for e in self.events if e["kind"]=="trial_result"][-1]
        self.assertEqual(result["reason"],"no_paired_target_frontal_reaction")

    def test_self_identity_change_incomplete(self):
        self.begin()
        self.assertEqual(self.send(row(100,actor=7))[-1]["label"],"incomplete")

    def test_missing_hp_incomplete(self):
        self.begin()
        self.assertEqual(self.send(row(100,48032100,hp=None))[-1]["reason"],"self_hp_unavailable")

    def test_missing_pre_sample_incomplete(self):
        result=self.send(row(10,48032100))[-1]
        self.assertEqual(result["label"],"incomplete")

    def test_counter_increment_only_describes_start_interval(self):
        self.send(row(0,requests=2),row(10,48032100,requests=3),
                  row(100,48030201,others=[peer(70030)],requests=3),
                  row(250,48030201,others=[peer(70030)],requests=3))
        result=[e for e in self.events if e["kind"]=="trial_result"][-1]
        self.assertEqual(result["automatic_status"],"unknown")
        self.assertEqual(result["automatic_request_increments"],1)
        self.assertEqual(result["input_origin"],"undetermined")

    def test_unchanged_counter_does_not_prove_manual(self):
        self.send(row(0,requests=2),row(10,48032100,requests=2))
        self.assertEqual(self.tracker.active["automatic_status"],"unknown")

    def test_trace_submission_before_animation_is_correlated(self):
        self.send(traced(90,sequence=4),traced(100,48032100,sequence=4))
        self.assertEqual(self.tracker.active['automatic_status'],'unknown')
        self.send(traced(115,48032100,sequence=4,confirmed=101))
        self.assertEqual(self.tracker.active['automatic_status'],'automatic_request_observed')
        self.assertEqual(self.tracker.active['automatic_trace_submission']['sequence'],4)
        self.assertEqual(self.tracker.active['requested_target_actor'],2)
        self.assertEqual(self.tracker.active['game_pid'],41)
        self.assertEqual(self.tracker.active['trace_session_id'],123)
        self.assertEqual(self.tracker.active['automatic_submission_sequence'],4)
        self.assertEqual(self.tracker.active['automatic_confirmed_tick32'],101)
        self.assertEqual(self.tracker.active['input_origin'],'undetermined')

    def test_stale_or_wrong_target_trace_is_not_correlated(self):
        for age,network_id in ((401,55),(50,56)):
            tracker=TrialTracker(PARRIES)
            tracker.update(traced(490))
            sample=traced(500,48032100,sequence=4,submitted=500-age,confirmed=500,target_net=network_id)
            tracker.update(sample)
            self.assertEqual(tracker.active['automatic_status'],'unknown')

    def test_submission_two_seconds_after_start_belongs_to_next_parry(self):
        self.send(traced(0),traced(10,48032100),traced(100),traced(2010,sequence=8,submitted=2005,requests=1))
        self.assertEqual(self.tracker.active['automatic_request_increments'],0)
        self.assertEqual(self.tracker.active['automatic_status'],'unknown')
        old=self.send(traced(2160,48032100,sequence=8,submitted=2005,confirmed=2160,requests=1))[-1]
        self.assertEqual(old['reason'],'next_parry_before_frontal_execution')
        self.assertIsNone(old['automatic_trace_submission'])
        self.assertEqual(self.tracker.active['automatic_submission_sequence'],8)

    def test_one_receipt_cannot_label_two_adjacent_animations(self):
        self.send(traced(95,sequence=4),traced(100,48032100,sequence=4,confirmed=99),
                  traced(105,0,sequence=4,confirmed=99))
        old=self.send(traced(110,48032100,sequence=4,confirmed=99))[-1]
        self.assertEqual(old['automatic_submission_sequence'],4)
        self.assertIsNone(self.tracker.active['automatic_submission_sequence'])

    def test_pid_session_build_and_missing_module_boundaries(self):
        for pid,new_session,missing_module in ((50,123,False),(41,124,False),(41,None,True)):
            with self.subTest(pid=pid,session=new_session,missing_module=missing_module):
                tracker=TrialTracker(PARRIES)
                tracker.update(traced(80,sequence=4))
                tracker.update(traced(90,48032100,sequence=4))
                next_row=traced(105,pid=pid,session=new_session)
                if missing_module:
                    next_row['automatic']={}
                transitioned=tracker.update(next_row)
                self.assertEqual(transitioned[0]['reason'],'game_or_trace_instance_changed')
                self.assertEqual(transitioned[0]['game_pid'],41)
                self.assertIsNone(transitioned[0]['automatic_submission_sequence'])
                stale=traced(115,48032100,pid=pid,session=new_session,sequence=4,confirmed=115)
                stale['automatic']['trace']['recent_parry_submission'].update(game_pid=41,session_id=123)
                if missing_module:
                    stale['automatic']['build_id']=None
                tracker.update(stale)
                self.assertEqual(tracker.active['automatic_status'],'unknown')

    def test_same_pid_new_session_can_reuse_sequence(self):
        self.send(traced(95),traced(100,48032100,sequence=4,confirmed=100))
        self.assertEqual(self.tracker.active['automatic_submission_sequence'],4)
        self.send(traced(110,session=124),traced(125,48032100,session=124,sequence=4,submitted=115,confirmed=125))
        self.assertEqual(self.tracker.active['automatic_submission_sequence'],4)
        self.assertEqual(self.tracker.active['trace_session_id'],124)

    def test_confirmation_requires_same_transaction_clip_and_onset(self):
        for field,value in (('phase',1),('kind',1),('self',88),('target',77),('tool',901),
                            ('previous_animation',42036900),('submitted_tick32',31),('confirmed_tick32',90)):
            with self.subTest(field=field):
                tracker=TrialTracker(PARRIES);tracker.update(traced(145))
                sample=traced(150,48032100,sequence=4,confirmed=150)
                sample['automatic']['defense'][field]=value
                tracker.update(sample)
                self.assertEqual(tracker.active['automatic_status'],'unknown')

    def test_missing_confirmation_cannot_establish_origin(self):
        self.send(traced(90))
        sample=traced(100,48032100,sequence=4,confirmed=100)
        del sample['automatic']['defense']
        self.send(sample)
        result=self.tracker.flush('game_process_ended')[0]
        self.assertIsNone(result['automatic_trace_submission'])
        self.assertEqual(result['input_origin'],'undetermined')

    def test_confirmation_across_tick_wrap(self):
        elapsed=1 << 32
        self.send(traced(elapsed-4),traced(elapsed+2,48032100,sequence=4,
            submitted=elapsed-100,confirmed=elapsed))
        self.assertEqual(self.tracker.active['automatic_submission_sequence'],4)
        self.assertEqual(self.tracker.active['automatic_confirmed_tick32'],0)

    def test_unverified_trace_header_does_not_claim_or_change_run(self):
        self.send(traced(90,sequence=4),traced(100,48032100,sequence=4))
        retry=traced(110,48032100,sequence=4,confirmed=100,session=999)
        retry['automatic']['trace']['state']='header_changed_retry'
        self.send(retry)
        self.assertEqual(self.tracker.active['trace_session_id'],123)
        self.assertIsNone(self.tracker.active['automatic_submission_sequence'])
        self.send(traced(120,48032100,sequence=4,confirmed=100))
        self.assertEqual(self.tracker.active['automatic_submission_sequence'],4)

    def test_request_cannot_cross_character_reuse_before_onset(self):
        self.send(traced(85,sequence=4,generation=1),traced(90,sequence=4,generation=2),
                  traced(100,48032100,sequence=4,confirmed=100,generation=2))
        self.assertIsNone(self.tracker.active['automatic_submission_sequence'])

    def test_first_verified_module_metadata_keeps_existing_trial_unattributed(self):
        before=traced(80);before['automatic']={}
        onset=traced(90,48032100);onset['automatic']={}
        self.send(before,onset)
        trial_id=self.tracker.active['trial_id']
        observed=self.send(traced(105,48032100,sequence=4,confirmed=90))
        self.assertFalse(observed)
        self.assertEqual(self.tracker.active['trial_id'],trial_id)
        self.assertIsNone(self.tracker.active['automatic_submission_sequence'])
        self.assertIsNone(self.tracker.active['trace_session_id'])

    def test_inconsistent_sample_age_or_newer_submit_cannot_label_onset(self):
        for submitted,forged_age in ((35,3),(105,0)):
            with self.subTest(submitted=submitted):
                tracker=TrialTracker(PARRIES);tracker.update(traced(95))
                sample=traced(100,48032100,sequence=4,submitted=submitted,confirmed=105)
                sample['automatic']['trace']['recent_parry_submission']['age_ms']=forged_age
                tracker.update(sample)
                self.assertIsNone(tracker.active['automatic_submission_sequence'])

    def test_late_target_pair_fails(self):
        self.begin()
        result=self.send(row(100,48030201),row(601,48030201,others=[peer(70030)]))[-1]
        self.assertEqual(result["reason"],"no_paired_target_frontal_reaction")

    def test_pair_can_arrive_after_grace_before_pair_limit(self):
        self.begin()
        self.send(row(100,48030201),row(260,48030201))
        result=self.send(row(400,48030201,others=[peer(70030)]))[-1]
        self.assertEqual(result["label"],"success")

    def test_damage_during_execution_grace_fails(self):
        self.begin()
        result=self.send(row(100,48030201,others=[peer(70030)]),
                         row(240,48030201,hp=900,others=[peer(70030)]))[-1]
        self.assertEqual(result["reason"],"self_hp_loss")

    def test_intervening_heavy_attack_fails(self):
        self.begin()
        result=self.send(row(100,74034320))[-1]
        self.assertEqual(result["reason"],"intervening_attack_before_execution")

    def test_flush_game_exit_is_incomplete(self):
        self.begin()
        result=self.tracker.flush("game_process_ended")[-1]
        self.assertEqual(result["label"],"incomplete")
        self.assertEqual(result["reason"],"game_process_ended")
        self.assertIsNone(self.tracker.previous)

    def test_missing_identity_is_incomplete(self):
        self.begin()
        self.assertEqual(self.send(row(100,actor=None))[-1]["label"],"incomplete")

    def test_target_identity_loss_is_incomplete(self):
        self.begin()
        self.assertEqual(self.send(row(100,48032100,others=[]))[-1]["reason"],"target_identity_lost_or_replaced")

    def test_same_self_pointer_new_generation_is_incomplete(self):
        samples=[row(0),row(10,48032100),row(100,48030201,others=[peer(70030)])]
        for i,sample in enumerate(samples):
            sample["self"].update(entity_id=100 if i<2 else 101,generation=1 if i<2 else 2)
        result=self.send(*samples)[-1]
        self.assertEqual(result["reason"],"self_identity_lost_or_changed")
        self.assertEqual(result["label"],"incomplete")

    def test_same_target_pointer_new_entity_cannot_pair(self):
        samples=[row(0),row(10,48032100),row(100,48030201,others=[peer(70030)])]
        for i,sample in enumerate(samples):
            sample["others"][0].update(entity_id=200 if i<2 else 201,generation=1 if i<2 else 2)
        result=self.send(*samples)[-1]
        self.assertEqual(result["reason"],"target_identity_lost_or_replaced")
        self.assertEqual(result["label"],"incomplete")
        self.assertIsNone(result["paired_target_actor"])

    def test_trial_preserves_start_context_and_network_reference(self):
        self.send(row(0))
        sample=row(10,48032100)
        sample["network_snapshot_id"]=41
        sample["self"]["equipment"]={"effective_tool_raw":20080000}
        sample["others"][0].update(entity_id=200,generation=1,distance_to_self=1.2,equipment={"right_selected_raw":6020000})
        # Establish the peer's identity in the previous frame too.
        self.tracker.previous["others"][2].update(entity_id=200,generation=1)
        self.send(sample)
        sample["self"]["equipment"]["effective_tool_raw"]=1
        result=self.tracker.flush("user_stop")[-1]
        self.assertEqual(result["start_network_snapshot_id"],41)
        self.assertEqual(result["start_self_context"]["equipment"]["effective_tool_raw"],20080000)
        self.assertEqual(result["start_target_context"][0]["distance_to_self"],1.2)

    def test_reaction_before_execution_can_pair(self):
        self.begin()
        self.send(row(100,48032100,others=[peer(70030)]),row(200,48030201,others=[peer(70030)]))
        self.assertEqual(self.send(row(350,48030201,others=[peer(70030)]))[-1]["label"],"success")

    def test_ambiguous_victims_are_not_success(self):
        others=[peer(),peer(actor=3,slot=2)]
        self.send(row(0,others=others),row(10,48032100,others=others))
        result=self.send(row(100,48030201,others=[peer(70030),peer(70030,actor=3,slot=2)]))[-1]
        self.assertEqual(result["reason"],"ambiguous_frontal_execution_target")

    def test_clock_restarts_incomplete(self):
        self.begin()
        result=self.send(row(0))[-1]
        self.assertEqual(result["reason"],"sampling_clock_restarted")

    def test_rolling_trial_storage_bounded(self):
        for t in range(100):
            self.send(row(t*100,others=[peer(70030 if t%2 else 0)]))
        self.assertLessEqual(len(self.tracker.reactions),32)
        self.assertFalse(hasattr(self.tracker,"trials"))


if __name__=="__main__":
    unittest.main()
