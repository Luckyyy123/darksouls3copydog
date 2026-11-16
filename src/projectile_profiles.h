#ifndef COPYDOG_PROJECTILE_PROFILES_H
#define COPYDOG_PROJECTILE_PROFILES_H

/* DS3 Bullet.param v3. Entity radius only; not the player's hurtbox or a
   guarantee of constant velocity. See work/projectile_profile_sources.md. */
static inline float CopydogVerifiedBoltRadius(int id)
{
    switch(id){
    case 500:                                /* basic Standard Arrow */
    case 800: case 805: case 808:            /* Standard Bolt */
    case 810: case 815: case 818:            /* Heavy Bolt */
    case 820: case 825: case 828:            /* Sniper Bolt */
    case 830: case 835: case 838:            /* Wood Bolt */
        return 0.02f;
    default:
        return -1.f;                         /* no verified direct-flight profile */
    }
}
#endif
