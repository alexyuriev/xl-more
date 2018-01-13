xl-more: A slightly more advanced X11 screen lock
=================================================

This is a derivative of Danny Dulai xl screen lock. While it is rather basic, its limited functionality
makes it quite a bit less prone to crashing.

Unlike his xl, xl-more:

1. Blacks out a screen ( including multi-monitor screen ).
2. Indicates via color of the screen if it is ignoring or accepting keystrokes for future unlock password validation.
3. Only validates the unlock password using PAM.
4. Customizable via Xresources.

### Functionality

XL-more lock has two distinct modes that are described below:

##### Lock-and-Ignore mode

When xl-more runs, it turns off a mouse cursor, intercepts a keyboard and a mouse input events, and creates a window covering the entire screen possibly spanning multiple monitors with the color defined in the `XL-more.color.ignore` Xresource. If the color is not defined, xl-more will use black color. All key pressed are ignored until a user at the keyboard hits the`[Enter]` key, switching the xl-more into the Lock-and-Store mode.

##### Lock-and-Store mode

This mode is indicated by the change of the screen color to the color defined in the `XL-more.color.store` Xresource. If the resource `XL-more.color.store` is not defined, xl-more will use blue screen. In this mode, xl-more will remember the key presses until the user hits the `[Enter]` again. After xl-more receives the `[Enter]` key in Lock-and-Store mode, it will attempt to validate the entered password using PAM. If the validation is successful, xl-more will unlock the system, restore the content of the screen and terminate. Otherwise, xl-more will switch to Lock-And-Ignore mode.

xl-more identifies itself to Linux PAM subsystem based on the string of the `XL-more.pam_service` Xresource. resource. If this resource is not defined, x-more checks `PAM_SERVICE` environment variable and uses the string defined in it as the service name. xl-more will not lock the screen if neither `XL-more.pam_service` Xresource nor `PAM_SERVICE` environment variable is defined. It is recommended that the service name for PAM matches the name of the service used by a user to login into X. Since I use LightDM, I define the service name as `lightdm`.

xl-more logs locking of the screen and both successful and unsuccessful attempts to unlock the screen to syslog.

```
alex@wrks-2:~/zubrcom/xl$ PAM_SERVICE=lightdm xl-more
```

The following is the Xresource section that I use.

```
! XL-more , with alex changes
XL-more.pam_service: lightdm
XL-more.color.ignore: #000000
XL-more.color.store:  #A9A9A9
```

#### Development and testing

xl-more was developed because power management on two of mine 28" 4K monitors sucks: every time the monitors went to sleep, upon wake up I needed to turn up volume to 1 bar followed by lowering it to 0 bars to kill off an annoying buzzing sound and all existing screen locking systems not only insisted on doing weird crap to my screen ( and crashing ) but also ignored no-DPMS management switching and forced me to tweak my monitors. So I found Danny's XL and XL-more was born.

