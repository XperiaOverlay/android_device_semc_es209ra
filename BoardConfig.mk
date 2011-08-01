TARGET_BOOTLOADER_BOARD_NAME := es209ra
TARGET_SPECIFIC_HEADER_PATH := device/semc/es209ra/include

TARGET_BOARD_PLATFORM := qsd8k
TARGET_BOARD_PLATFORM_GPU := qcom-adreno200

TARGET_CPU_ABI := armeabi-v7a
TARGET_CPU_ABI2 := armeabi
TARGET_ARCH_VARIANT := armv7-a-neon
ARCH_ARM_HAVE_TLS_REGISTER := true
ARCH_ARM_HAVE_ARMV7A_BUG := true


BOARD_USES_GENERIC_AUDIO := false
TARGET_PROVIDES_LIBAUDIO := true
#TARGET_PROVIDES_LIBRIL := false

# Wifi related defines
BOARD_WPA_SUPPLICANT_DRIVER := AWEXT
BOARD_WLAN_DEVICE           := wlan0
WIFI_DRIVER_MODULE_PATH     := "/system/lib/modules/ar6000.ko"
WIFI_DRIVER_MODULE_NAME     := "ar6000"

BOARD_HAVE_BLUETOOTH := true

BOARD_USES_QCOM_HARDWARE := true
BOARD_USES_QCOM_LIBS := true
BOARD_USES_QCOM_LIBRPC := true
#BOARD_USE_QCOM_PMEM := true

BOARD_USES_QCOM_GPS := true
BOARD_VENDOR_QCOM_AMSS_VERSION := 1240
BOARD_VENDOR_QCOM_GPS_LOC_API_HARDWARE := es209ra
BOARD_VENDOR_QCOM_GPS_LOC_API_AMSS_VERSION := 1240

BOARD_EGL_CFG := device/semc/msm7x30-common/prebuilt/egl.cfg
BOARD_NO_RGBX_8888 := true

#no need for those when new kernel is awailable
#BOARD_USE_USB_MASS_STORAGE_SWITCH := true
#TARGET_USE_CUSTOM_LUN_FILE_PATH := /sys/devices/platform/msm_hsusb/gadget/lun
#TARGET_USE_CUSTOM_VIBRATOR_FILE_PATH := /sys/devices/platform/msm_pmic_vibrator/enable

TARGET_RECOVERY_PRE_COMMAND := "touch /cache/recovery/boot;sync;"
BOARD_HAS_BOOT_RECOVERY := true
BOARD_HAS_SMALL_RECOVERY := true
BOARD_HAS_NO_MISC_PARTITION := true
BOARD_USES_RECOVERY_CHARGEMODE := false
BOARD_HAS_NO_SELECT_BUTTON := true
BOARD_HDPI_RECOVERY := true


BOARD_KERNEL_CMDLINE := console=null
BOARD_KERNEL_BASE := 0x20000000

BOARD_SDCARD_INTERNAL_DEVICE := /dev/block/mmcblk0p1

# A custom ota package maker for a device without a boot partition
TARGET_RELEASETOOL_OTA_FROM_TARGET_SCRIPT := device/semc/es209ra/releasetools/semc_ota_from_target_files

 BOARD_CUSTOM_BOOTIMG_MK = device/semc/es209ra/CustomKernel.mk