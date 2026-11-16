import unittest
from capture_action_names import NameEpochTracker, read_action_name


def actor(name,animation=0,weapon=6020310,mode=3,entity=55):
    return dict(slot=1,actor=100000,entity_id=entity,animation=animation,action_name=name,
                equipment=dict(left_selected_raw=11011410,right_selected_raw=weapon,hand_mode=mode))


class NameTests(unittest.TestCase):
    def begin(self):
        tracker=NameEpochTracker({42032100})
        tracker.update(actor('Idle'),0)
        tracker.update(actor('AttackRightLight1'),5)
        return tracker

    def test_pairs_after_12ms_and_backdates(self):
        tracker=self.begin()
        self.assertFalse(any(e['kind']=='action_name_attack_id_candidate' for e in tracker.update(actor('AttackRightLight1',25034000),25)))
        self.assertFalse(any(e['kind']=='action_name_attack_id_candidate' for e in tracker.update(actor('AttackRightLight1',25034000),36)))
        event=next(e for e in tracker.update(actor('AttackRightLight1',25034000),37) if e['kind']=='action_name_attack_id_candidate')
        self.assertEqual(event['lag_ms'],20)
        self.assertEqual(event['attack_id_first_t_ms'],25)

    def test_initial_attack_name_is_not_known_onset(self):
        tracker=NameEpochTracker()
        tracker.update(actor('AttackRightLight1'),0)
        tracker.update(actor('AttackRightLight1',25034000),10)
        self.assertFalse(any(e['kind']=='action_name_attack_id_candidate' for e in tracker.update(actor('AttackRightLight1',25034000),30)))

    def test_unchanged_id_not_paired(self):
        tracker=NameEpochTracker()
        tracker.update(actor('Idle',25034000),0)
        tracker.update(actor('AttackRightLight1',25034000),5)
        self.assertFalse(any(e['kind']=='action_name_attack_id_candidate' for e in tracker.update(actor('AttackRightLight1',25034000),25)))

    def test_name_change_before_stability_not_paired(self):
        tracker=self.begin()
        tracker.update(actor('AttackRightLight1',25034000),20)
        events=tracker.update(actor('Idle',25034000),35)
        self.assertFalse(any(e['kind']=='action_name_attack_id_candidate' for e in events))

    def test_context_change_breaks_pair(self):
        for key,value in (('weapon',6020410),('mode',1),('entity',56)):
            tracker=self.begin()
            tracker.update(actor('AttackRightLight1',25034000),20)
            events=tracker.update(actor('AttackRightLight1',25034000,**{key:value}),40)
            self.assertFalse(any(e['kind']=='action_name_attack_id_candidate' for e in events))

    def test_transient_attack_id_is_not_stable(self):
        tracker=self.begin()
        tracker.update(actor('AttackRightLight1',25034000),20)
        tracker.update(actor('AttackRightLight1',0),25)
        self.assertFalse(any(e['kind']=='action_name_attack_id_candidate' for e in tracker.update(actor('AttackRightLight1',0),40)))

    def test_simultaneous_name_and_id_change_can_pair(self):
        tracker=NameEpochTracker()
        tracker.update(actor('Idle'),0)
        tracker.update(actor('AttackRightLight1',25034000),10)
        events=tracker.update(actor('AttackRightLight1',25034000),25)
        self.assertEqual(next(e for e in events if e['kind']=='action_name_attack_id_candidate')['lag_ms'],0)

    def test_known_parry_not_treated_as_attack_id(self):
        tracker=self.begin()
        tracker.update(actor('AttackRightLight1',42032100),20)
        self.assertFalse(any(e['kind']=='action_name_attack_id_candidate' for e in tracker.update(actor('AttackRightLight1',42032100),40)))

    def test_gap_does_not_prove_stability(self):
        tracker=self.begin()
        tracker.update(actor('AttackRightLight1',25034000),20)
        self.assertFalse(any(e['kind']=='action_name_attack_id_candidate' for e in tracker.update(actor('AttackRightLight1',25034000),200)))

    def test_missing_equipment_does_not_prove_same_weapon(self):
        tracker=NameEpochTracker()
        for t,name,animation in ((0,'Idle',0),(5,'AttackRightLight1',0),(20,'AttackRightLight1',25034000),(40,'AttackRightLight1',25034000)):
            sample=actor(name,animation);sample['equipment']=None
            events=tracker.update(sample,t)
            self.assertFalse(any(e['kind']=='action_name_attack_id_candidate' for e in events))

    def test_name_decode_exact_pointer_chain(self):
        class Reader:
            def ptr(self,address):return {100000+0x1f90:200000,200000+0x28:300000}.get(address,0)
            def bytes(self,address,count):
                self_address=address
                assert self_address==300000+0x898 and count==160
                return ('AttackRightLight1\0'.encode('utf-16-le')+b'\x00\xd8').ljust(160,b'\0')
        self.assertEqual(read_action_name(Reader(),100000),('AttackRightLight1','read'))


if __name__=='__main__':unittest.main()
