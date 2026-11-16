import unittest

from ranged_review import classify_event, RangedReview, SOURCE


def obs(name, epoch=3, slot=1, **values):
    return dict(dict(kind='action_name_or_id_changed',t_ms=100,slot=slot,epoch=epoch,
                     run_id='test',context_same=True,name_onset_observed=True,
                     animation_changed=False,actor=dict(action_name=name,action_name_read_quality='read',
                        actor=20,entity_id=123,animation=999,network_id=456)),**values)


class RangedReviewTests(unittest.TestCase):
    def test_bow_stages_are_not_projectiles(self):
        for s,stage in (('Start','ready_animation'),('Loop','hold_animation'),
                        ('Fire','fire_animation'),('Reload','reload_animation')):
            v=classify_event(obs('AttackCrossbowLeft'+s), SOURCE)
            self.assertEqual((v['family'],v['stage'],v['side']),('crossbow',stage,'remote'))
            self.assertIsNone(v['projectile_id'])
            self.assertEqual(set(v),{'action_name','t_ms','slot','side','family','stage','evidence',
                                     'projectile_id','spell_id','school'})
        self.assertEqual(classify_event(obs('AttackBowRightShoot',slot=0), SOURCE)['side'],'self')

    def test_spell_name_hints_do_not_identify_spell_or_damage(self):
        v=classify_event(obs('AttackMagicRightStart'), SOURCE)
        self.assertEqual((v['family'],v['stage'],v['spell_id'],v['school']),('spell','cast_animation',None,None))
        for name in ('DamageMiddle','AttackerName','MagicDamage','CastWithoutKnownGrammar',''):
            self.assertIsNone(classify_event(obs(name),SOURCE))

    def test_raw_stream_dedup_and_reentry(self):
        c=RangedReview()
        rows=[obs('Idle',None,name_onset_observed=False),obs('AttackCrossbowLeftFire',animation_changed=True),
              obs('AttackCrossbowLeftFire',animation_changed=True),obs('AttackCrossbowLeftReload',4),
              obs('AttackCrossbowLeftFire',5),obs('MagicStart',None),obs('MagicStart',None,animation_changed=True),
              obs('Idle',None),obs('MagicStart',None)]
        self.assertEqual([v['stage'] for r in rows if (v:=c.consume(r,SOURCE))],
                         ['fire_animation','reload_animation','fire_animation','cast_animation','cast_animation'])

    def test_first_id_only_restart_gap_and_unavailable(self):
        c=RangedReview()
        row=obs('AttackCrossbowLeftFire',animation_changed=True)
        self.assertIsNone(c.consume(row,SOURCE))
        self.assertIsNone(c.consume(obs('AttackCrossbowLeftFire',run_id='new',context_same=False),SOURCE))
        c.consume(dict(kind='actor_unavailable',slot=1,run_id='new'),SOURCE)
        self.assertIsNone(c.consume(dict(row,run_id='new'),SOURCE))

    def test_unsupported_sources_and_malformed_times_abstain(self):
        row=obs('AttackCrossbowLeftFire',wall='2026-11-15T19:16:00+08:00',perf_counter_ns=888)
        v=classify_event(row,'C:/sample/'+SOURCE)
        self.assertEqual(v['perf_counter_ns'],888)
        self.assertIn('+08:00',v['wall'])
        self.assertIsNone(classify_event(row,'projectiles.jsonl'))
        self.assertIsNone(classify_event(dict(row,kind='action_name_attack_id_candidate'),SOURCE))
        for update in [dict(t_ms=float('nan')),dict(t_ms=-1),dict(slot=True),dict(slot=6),
                       dict(actor={'action_name':'AttackBowLeftFire','action_name_read_quality':'unavailable'})]:
            self.assertIsNone(classify_event(dict(row,**update),SOURCE))
