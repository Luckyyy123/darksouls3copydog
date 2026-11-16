#ifndef COPYDOG_PARRY_ANIMATIONS_H
#define COPYDOG_PARRY_ANIMATIONS_H
/* Type-64 intervals decoded in animation_frame_audit_20261112/parry_behavior_windows.csv.
   Recognition confirms an animation with a parry behavior, never a successful parry. */
#define COPYDOG_ANIMATION_STABLE_MS 12u
static inline int CopydogKnownParryAnimation(int animation)
{
    switch(animation){
    case 800110: case 20032100: case 20032110: case 20032120: case 20032140: case 23032100:
    case 23032110: case 23032120: case 23036900: case 23037320: case 27032100: case 27032110:
    case 27032120: case 27036900: case 28032100: case 28032110: case 28032120: case 28032140:
    case 29036240: case 29036241: case 42032100: case 42032110: case 42032120: case 42032140:
    case 42036900: case 43032100: case 43032110: case 43032120: case 43032140: case 48032100:
    case 48032101: case 48032110: case 48032111: case 48032120: case 48032121: case 48032140:
    case 48036900: case 48036901: case 71032100: case 71032101: case 71032110: case 71032111:
    case 71032120: case 71032121: case 71036900: case 71036901: case 91032100: case 91032101:
    case 91032110: case 91032111: case 91032120: case 91032121: case 91036900: case 91036901:
    case 99032000: case 99032030: case 99032060: case 101032000: case 101032010: case 101032020:
    case 101032030: case 101032060: case 130032100: case 130032110: case 130032120: case 130032140:
    case 137032000: case 137032010: case 137032020: case 137032030: case 137032060: case 144036240:
    case 144036241: case 150032100: case 150032101: case 150032110: case 150032111: case 150032120:
    case 150032121: case 150036900: case 150036901: case 151032000: case 151032010: case 151032020:
    case 151032030: case 151032060: case 161038400: case 161038401: case 164032000: case 164032010:
    case 164032020: case 164032030: case 164032060: case 214032100: case 214032101: case 214032110:
    case 214032111: case 214032120: case 214032121: case 214036900: case 214036901: case 218032000:
    case 218032030: case 218032060:
        return 1;
    default:return 0;
    }
}
#endif
