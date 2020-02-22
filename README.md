# BGFTP
Background FTP server for PS Vita.

Works simultaneously with any game, including enlarged memory mode games, and in sleep mode. Runs on system-reserved core, so performance in games is not affected. BGFTP can send various information to user via notifications.

Can be somewhat slow when cpu-hungry game process is running due to BGFTP process having low priority.

# How to use

1. Install [LowMemMode plugin](https://github.com/GrapheneCt/LowMemMode) to increase multitasking abilities. It is not required, but highly recommended.
2. Intstall .vpk, start BGFTP application.
3. Press X button to start BGFTP background application.
4. Now you can use BGFTP.

To disable notifications, go to Settings -> Notifications -> BGFTP.
Don't forget to terminate BGFTP after you finished using it, otherwise you system will not switch to sleep mode.

#### BGFTP background application can be terminated under following conditions:

1. LiveArea of the main application is peeled off.
2. Enlarged memory mode game is started. BGFTP can be relaunched afterwards if you have [LowMemMode plugin](https://github.com/GrapheneCt/LowMemMode) installed.

# How to build
This application uses [libvita2d_sys](https://github.com/GrapheneCt/libvita2d_sys) and modified version of libftpvita.

Use vitasdk to build application. After building, boot params for both executables must be set manually. Refer to the precompiled version for the values.

# Credits

This application uses modified versions of libftpvita and libvita2d by xerpi.
