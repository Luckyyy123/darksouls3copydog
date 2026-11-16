#ifndef COPYDOG_PROJECTILE_GEOMETRY_H
#define COPYDOG_PROJECTILE_GEOMETRY_H

/* World units above the actor root (Y up). Approximation, not a native
   player hurtbox. Add the projectile radius to the body radius. */
#define COPYDOG_PROJECTILE_BODY_LOW_Y   0.4
#define COPYDOG_PROJECTILE_BODY_HIGH_Y  1.2
#define COPYDOG_PROJECTILE_BODY_RADIUS  0.4
#define COPYDOG_PROJECTILE_HORIZON      0.5

/* Linear translation only. First surface contact in [0,horizon], seconds;
   already touching/inside returns 0 seconds. Miss/invalid sets *out=-1.
   lowY==highY is a sphere. All four vectors and an output are required. */
int CopydogProjectileContact(const float p[3],const float v[3],const float me[3],
    const float playerV[3],double lowY,double highY,double combinedRadius,double horizon,double *out);

#endif
