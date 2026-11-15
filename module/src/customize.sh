# shellcheck disable=SC2034
SKIPUNZIP=1

DEBUG=@DEBUG@
MIN_APATCH_VERSION=@MIN_APATCH_VERSION@
MIN_KSU_VERSION=@MIN_KSU_VERSION@
MIN_KSUD_VERSION=@MIN_KSUD_VERSION@
MAX_KSU_VERSION=@MAX_KSU_VERSION@
MIN_MAGISK_VERSION=@MIN_MAGISK_VERSION@

if [ "$BOOTMODE" ] && [ "$APATCH" ]; then
  ui_print "- Installing from APatch app"
  if ! [ "$APATCH_VER_CODE" ] || [ "$APATCH_VER_CODE" -lt "$MIN_APATCH_VERSION" ]; then
    ui_print "*********************************************************"
    ui_print "! APatch version is too old!"
    ui_print "! Please update APatch to latest version"
    abort    "*********************************************************"
  fi
  if [ "$(which magisk)" ]; then
    ui_print "*********************************************************"
    ui_print "! Multiple root implementation is NOT supported!"
    ui_print "! Please uninstall Magisk before installing OnyxZygisk"
    abort    "*********************************************************"
  fi
elif [ "$BOOTMODE" ] && [ "$KSU" ]; then
  ui_print "- Installing from KernelSU app"
  ui_print "- KernelSU version: $KSU_KERNEL_VER_CODE (kernel) + $KSU_VER_CODE (ksud)"
  if ! [ "$KSU_KERNEL_VER_CODE" ] || [ "$KSU_KERNEL_VER_CODE" -lt "$MIN_KSU_VERSION" ]; then
    ui_print "*********************************************************"
    ui_print "! KernelSU version is too old!"
    ui_print "! Please update KernelSU to latest version"
    abort    "*********************************************************"
  elif [ "$KSU_KERNEL_VER_CODE" -ge "$MAX_KSU_VERSION" ]; then
    ui_print "*********************************************************"
    ui_print "! KernelSU version too large!"
    ui_print "! Support for KernelSU (variant) could be incomplete"
    ui_print "*********************************************************"
  fi
  if ! [ "$KSU_VER_CODE" ] || [ "$KSU_VER_CODE" -lt "$MIN_KSUD_VERSION" ]; then
    ui_print "*********************************************************"
    ui_print "! ksud version is too old!"
    ui_print "! Please update KernelSU Manager to latest version"
    abort    "*********************************************************"
  fi
  if [ "$(which magisk)" ]; then
    ui_print "*********************************************************"
    ui_print "! Multiple root implementation is NOT supported!"
    ui_print "! Please uninstall Magisk before installing OnyxZygisk"
    abort    "*********************************************************"
  fi
elif [ "$BOOTMODE" ] && [ "$MAGISK_VER_CODE" ]; then
  ui_print "- Installing from Magisk app"
  if [ "$MAGISK_VER_CODE" -lt "$MIN_MAGISK_VERSION" ]; then
    ui_print "*********************************************************"
    ui_print "! Magisk version is too old!"
    ui_print "! Please update Magisk to latest version"
    abort    "*********************************************************"
  fi
else
  ui_print "*********************************************************"
  ui_print "! Install from recovery is not supported"
  ui_print "! Please install from APatch, KernelSU or Magisk app"
  abort    "*********************************************************"
fi

VERSION=$(grep_prop version "${TMPDIR}/module.prop")
ui_print "- Installing OnyxZygisk $VERSION"

# Installing/updating the module is an explicit request to retry injection.
# Clear a previously latched boot-loop recovery state; otherwise the new
# binary is installed successfully but its monitor exits before doing any
# work, which looks exactly like a broken hot-plug switch.  Preserve the
# user's hotplug/ and mount-mode preferences.
ONYX_WORKDIR=/data/adb/onyxzygisk
if [ -f "$ONYX_WORKDIR/injection_disabled" ]; then
  ui_print "- Clearing stale boot-loop fail-safe latch"
fi
rm -f "$ONYX_WORKDIR/injection_disabled" \
  "$ONYX_WORKDIR/boot_fail_count" \
  "$ONYX_WORKDIR/boot_fail_boot_id" \
  "$ONYX_WORKDIR/hotplug_restart_guard" \
  "$ONYX_WORKDIR"/hotplug_activated/*.restarted

# check android
if [ "$API" -lt 26 ]; then
  ui_print "! Unsupported sdk: $API"
  abort "! Minimal supported sdk is 26 (Android 8.0)"
else
  ui_print "- Device sdk: $API"
fi

# check architecture: this package contains ARM64 binaries only.
if [ "$ARCH" != "arm64" ]; then
  ui_print "*********************************************************"
  ui_print "! ARM64 only (arm64-v8a) is supported"
  ui_print "! Detected device architecture: $ARCH"
  abort    "*********************************************************"
fi
ui_print "- Device platform: ARM64 (arm64-v8a)"

ui_print "- Extracting verify.sh"
unzip -o "$ZIPFILE" 'verify.sh' -d "$TMPDIR" >&2
if [ ! -f "$TMPDIR/verify.sh" ]; then
  ui_print "*********************************************************"
  ui_print "! Unable to extract verify.sh!"
  ui_print "! This zip may be corrupted, please try downloading again"
  abort    "*********************************************************"
fi
. "$TMPDIR/verify.sh"
extract "$ZIPFILE" 'customize.sh'  "$TMPDIR/.vunzip"
extract "$ZIPFILE" 'verify.sh'     "$TMPDIR/.vunzip"
extract "$ZIPFILE" 'sepolicy.rule' "$TMPDIR"

if [ "$KSU" ]; then
  ui_print "- Checking SELinux patches"
  if ! check_sepolicy "$TMPDIR/sepolicy.rule"; then
    ui_print "*********************************************************"
    ui_print "! Unable to apply SELinux patches!"
    ui_print "! Your kernel may not support SELinux patch fully"
    abort    "*********************************************************"
  fi
fi

ui_print "- Extracting module files"
extract "$ZIPFILE" 'action.sh'     "$MODPATH"
extract "$ZIPFILE" 'module.prop'     "$MODPATH"
extract "$ZIPFILE" 'post-fs-data.sh' "$MODPATH"
extract "$ZIPFILE" 'late-load.sh'    "$MODPATH"
extract "$ZIPFILE" 'zygisk-init.sh'  "$MODPATH"
extract "$ZIPFILE" 'service.sh'      "$MODPATH"
extract "$ZIPFILE" 'uninstall.sh'      "$MODPATH"
mv "$TMPDIR/sepolicy.rule" "$MODPATH"

ui_print "- Extracting WebUI (webroot)"
# With SKIPUNZIP=1 the webroot must be extracted manually, or the manager won't find the page
unzip -o "$ZIPFILE" 'webroot/*' -d "$MODPATH" >&2 || abort_verify "Failed to extract webroot"
# Checksum files are only used for install-time verification; don't keep them in the module
find "$MODPATH/webroot" -name '*.sha256' -delete
if [ ! -f "$MODPATH/webroot/index.html" ]; then
  ui_print "! Warning: webroot/index.html not found after extraction"
fi

# Remove binaries left by an older multi-ABI installation before extracting
# the ARM64-only payload.
mkdir -p "$MODPATH/bin" "$MODPATH/lib64"
rm -rf "$MODPATH/lib"
rm -f "$MODPATH/bin/zygiskd32" "$MODPATH/bin/zygisk-ptrace32"

ui_print "- Extracting ARM64 (arm64-v8a) libraries"
extract "$ZIPFILE" 'bin/arm64-v8a/zygiskd' "$MODPATH/bin" true
mv "$MODPATH/bin/zygiskd" "$MODPATH/bin/zygiskd64"
extract "$ZIPFILE" 'lib/arm64-v8a/libzygisk.so' "$MODPATH/lib64" true
extract "$ZIPFILE" 'lib/arm64-v8a/libzygisk_ptrace.so' "$MODPATH/bin" true
mv "$MODPATH/bin/libzygisk_ptrace.so" "$MODPATH/bin/zygisk-ptrace64"

ui_print "- Setting permissions"
set_perm_recursive "$MODPATH/bin" 0 0 0755 0755
set_perm_recursive "$MODPATH/lib64" 0 0 0755 0644 u:object_r:system_lib_file:s0
set_perm_recursive "$MODPATH/webroot" 0 0 0755 0644

# If Huawei's Maple is enabled, system_server is created with a special way which is out of Zygisk's control
HUAWEI_MAPLE_ENABLED=$(grep_prop ro.maple.enable)
if [ "$HUAWEI_MAPLE_ENABLED" == "1" ]; then
  ui_print "- Add ro.maple.enable=0"
  echo "ro.maple.enable=0" >>"$MODPATH/system.prop"
fi
