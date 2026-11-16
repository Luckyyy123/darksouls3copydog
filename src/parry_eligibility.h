#ifndef COPYDOG_PARRY_ELIGIBILITY_H
#define COPYDOG_PARRY_ELIGIBILITY_H
/* Resolved TAE: a195 R1 imports a51, AttackType0 intersects JumpTable5.
   These exact IDs were observed with Murakumo in the current capture.
   Do not generalize this exception to every two-hand curved-greatsword move. */
static inline int CopydogVerifiedParryException(int animation)
{
    return animation==195034000||animation==195034010||animation==195034020||animation==264034500;
}
#endif
