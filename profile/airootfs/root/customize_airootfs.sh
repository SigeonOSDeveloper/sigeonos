#!/usr/bin/env bash
set -e

# The sigeonos packages are built into the ISO already, and there is no
# public repository yet, so no [sigeonos] repo is configured on the medium.

# Allow LightDM to autologin as root on the live medium
if ! getent group autologin >/dev/null 2>&1; then
    groupadd -r autologin
fi
usermod -aG autologin root

# Single bottom taskbar (applications menu, window buttons, tray, clock, actions).
# Replaces the stock two-panel default (top taskbar + bottom dock) for all new sessions.
cat > /etc/xdg/xfce4/panel/default.xml <<'XML'
<?xml version="1.0" encoding="UTF-8"?>

<channel name="xfce4-panel" version="1.0">
  <property name="configver" type="int" value="2"/>
  <property name="panels" type="array">
    <value type="int" value="1"/>
    <property name="dark-mode" type="bool" value="true"/>
    <property name="panel-1" type="empty">
      <property name="position" type="string" value="p=10;x=0;y=0"/>
      <property name="length" type="uint" value="100"/>
      <property name="position-locked" type="bool" value="true"/>
      <property name="icon-size" type="uint" value="22"/>
      <property name="size" type="uint" value="32"/>
      <property name="plugin-ids" type="array">
        <value type="int" value="1"/>
        <value type="int" value="2"/>
        <value type="int" value="3"/>
        <value type="int" value="4"/>
        <value type="int" value="5"/>
        <value type="int" value="6"/>
        <value type="int" value="7"/>
      </property>
    </property>
  </property>
  <property name="plugins" type="empty">
    <property name="plugin-1" type="string" value="whiskermenu">
      <property name="button-icon" type="string" value="/usr/share/sigeonos/logo.png"/>
      <property name="button-title" type="string" value=""/>
    </property>
    <property name="plugin-2" type="string" value="tasklist">
      <property name="grouping" type="uint" value="1"/>
    </property>
    <property name="plugin-3" type="string" value="separator">
      <property name="expand" type="bool" value="true"/>
      <property name="style" type="uint" value="0"/>
    </property>
    <property name="plugin-4" type="string" value="separator">
      <property name="style" type="uint" value="0"/>
    </property>
    <property name="plugin-5" type="string" value="systray">
      <property name="square-icons" type="bool" value="true"/>
    </property>
    <property name="plugin-6" type="string" value="clock"/>
    <property name="plugin-7" type="string" value="actions"/>
  </property>
</channel>
XML

# Apply the Sigeon wallpaper to the live session (root) and to new users,
# seeding the per-user config that xfconfd reads directly.
mkdir -p /root/.config/xfce4/xfconf/xfce-perchannel-xml
cat > /root/.config/xfce4/xfconf/xfce-perchannel-xml/xfce4-desktop.xml <<'XML'
<?xml version="1.0" encoding="UTF-8"?>

<channel name="xfce4-desktop" version="1.0">
  <property name="backdrop" type="empty">
    <property name="screen0" type="empty">
      <property name="monitor0" type="empty">
        <property name="workspace0" type="empty">
          <property name="color-style" type="int" value="0"/>
          <property name="image-style" type="int" value="5"/>
          <property name="last-image" type="string" value="/usr/share/backgrounds/sigeonos/wallpaper.png"/>
        </property>
      </property>
    </property>
  </property>
</channel>
XML
install -Dm644 /root/.config/xfce4/xfconf/xfce-perchannel-xml/xfce4-desktop.xml /etc/skel/.config/xfce4/xfconf/xfce-perchannel-xml/xfce4-desktop.xml

# Papirus icon theme for all desktop/panel/app icons.
mkdir -p /etc/xdg/xfce4/xfconf/xfce-perchannel-xml
cat > /etc/xdg/xfce4/xfconf/xfce-perchannel-xml/xsettings.xml <<'XML'
<?xml version="1.0" encoding="UTF-8"?>

<channel name="xsettings" version="1.0">
  <property name="Net" type="empty">
    <property name="IconThemeName" type="string" value="Papirus"/>
  </property>
  <property name="Gtk" type="empty">
    <property name="CursorThemeName" type="string" value="Adwaita"/>
    <property name="CursorThemeSize" type="int" value="24"/>
  </property>
</channel>
XML
install -Dm644 /etc/xdg/xfce4/xfconf/xfce-perchannel-xml/xsettings.xml /root/.config/xfce4/xfconf/xfce-perchannel-xml/xsettings.xml
install -Dm644 /etc/xdg/xfce4/xfconf/xfce-perchannel-xml/xsettings.xml /etc/skel/.config/xfce4/xfconf/xfce-perchannel-xml/xsettings.xml

# Keep the Whisker menu compact (and always above the bottom taskbar) so a
# tall menu can never swallow the panel.
mkdir -p /root/.config/xfce4/panel
cat > /root/.config/xfce4/panel/whiskermenu-1.rc <<'RC'
menu-width=520
menu-height=420
RC
install -Dm644 /root/.config/xfce4/panel/whiskermenu-1.rc /etc/skel/.config/xfce4/panel/whiskermenu-1.rc

# xfdesktop 4.20 stores the backdrop under the real RandR monitor name
# (e.g. "Virtual-1"), so the generic "monitor0" seed above is ignored.
# Apply the wallpaper to the actual connected monitor at every login.
install -Dm755 /dev/stdin /usr/share/sigeonos/set-wallpaper.sh <<'SH'
#!/bin/bash
MON=$(xrandr --listmonitors 2>/dev/null | awk 'NR>1 { print $NF; exit }')
[ -z "$MON" ] && MON=0
P="/backdrop/screen0/monitor${MON}/workspace0"
xfconf-query -c xfce4-desktop --create -t int    -p "$P/image-style" -s 5
xfconf-query -c xfce4-desktop --create -t int    -p "$P/color-style" -s 0
xfconf-query -c xfce4-desktop --create -t string -p "$P/color1"      -s '#ffffff'
xfconf-query -c xfce4-desktop --create -t string -p "$P/color2"      -s '#ffffff'
xfconf-query -c xfce4-desktop --create -t string -p "$P/last-image"  -s /usr/share/backgrounds/sigeonos/wallpaper.png
xfconf-query -c xfce4-desktop -p "$P/image-style" -s 5
xfconf-query -c xfce4-desktop -p "$P/color-style" -s 0
xfconf-query -c xfce4-desktop -p "$P/color1"      -s '#ffffff'
xfconf-query -c xfce4-desktop -p "$P/color2"      -s '#ffffff'
xfconf-query -c xfce4-desktop -p "$P/last-image"  -s /usr/share/backgrounds/sigeonos/wallpaper.png
if command -v xfdesktop >/dev/null 2>&1; then
    xfdesktop --reload 2>/dev/null
fi
SH

install -Dm644 /dev/stdin /etc/xdg/autostart/sigeonos-wallpaper.desktop <<'DESK'
[Desktop Entry]
Type=Application
Name=Sigeon Wallpaper
Exec=/usr/share/sigeonos/set-wallpaper.sh
X-GNOME-Autostart-enabled=true
NoDisplay=true
DESK

exit 0
