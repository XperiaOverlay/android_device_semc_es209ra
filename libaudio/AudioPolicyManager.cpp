/*
 * Copyright (C) 2009 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#define LOG_TAG "AudioPolicyManager"
//#define LOG_NDEBUG 0
#include <utils/Log.h>
#include "AudioPolicyManager.h"
#include <media/mediarecorder.h>

namespace android_audio_legacy {


// Max volume for streams when playing over bluetooth SCO device while in call: -18dB
#define IN_CALL_SCO_VOLUME_MAX  0.126
// Min music volume for 3.5mm jack in car dock: -10dB
#define CAR_DOCK_MUSIC_MINI_JACK_VOLUME_MIN 0.316

// ----------------------------------------------------------------------------
// AudioPolicyManager implementation for qsd8k platform
// Common audio policy manager code is implemented in AudioPolicyManagerBase class
// ----------------------------------------------------------------------------

// ---  class factory


extern "C" AudioPolicyInterface* createAudioPolicyManager(AudioPolicyClientInterface *clientInterface)
{
    return new AudioPolicyManager(clientInterface);
}

extern "C" void destroyAudioPolicyManager(AudioPolicyInterface *interface)
{
    delete interface;
}

// ---


audio_devices_t AudioPolicyManager::getDeviceForStrategy(routing_strategy strategy, bool fromCache)
{
    uint32_t device = 0;

    if (fromCache) {
        ALOGV("getDeviceForStrategy() from cache strategy %d, device %x", strategy, mDeviceForStrategy[strategy]);
        return mDeviceForStrategy[strategy];
    }

    switch (strategy) {
    case STRATEGY_SONIFICATION_RESPECTFUL:
        if (isInCall()) {
            device = getDeviceForStrategy(STRATEGY_SONIFICATION, false /*fromCache*/);
        } else if (isStreamActive(AudioSystem::MUSIC, SONIFICATION_RESPECTFUL_AFTER_MUSIC_DELAY)) {
            // while media is playing (or has recently played), use the same device
            device = getDeviceForStrategy(STRATEGY_MEDIA, false /*fromCache*/);
        } else {
            // when media is not playing anymore, fall back on the sonification behavior
            device = getDeviceForStrategy(STRATEGY_SONIFICATION, false /*fromCache*/);
        }

        break;

    case STRATEGY_DTMF:
        if (!isInCall()) {
            // when off call, DTMF strategy follows the same rules as MEDIA strategy
            device = getDeviceForStrategy(STRATEGY_MEDIA, false);
            break;
        }
        // when in call, DTMF and PHONE strategies follow the same rules
        // FALL THROUGH

    case STRATEGY_PHONE:
        // for phone strategy, we first consider the forced use and then the available devices by order
        // of priority
        switch (mForceUse[AudioSystem::FOR_COMMUNICATION]) {
        case AudioSystem::FORCE_BT_SCO:
            if (!isInCall() || strategy != STRATEGY_DTMF) {
                device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_BLUETOOTH_SCO_CARKIT;
                if (device) break;
            }
            // otherwise (not docked) continue with selection
            device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_BLUETOOTH_SCO_HEADSET;
            if (device) break;
            device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_BLUETOOTH_SCO;
            if (device) break;
            // if SCO device is requested but no SCO device is available, fall back to default case
            // FALL THROUGH

        default:    // FORCE_NONE
            device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_WIRED_HEADPHONE;
            if (device) break;
            device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_WIRED_HEADSET;
            if (device) break;
            // when not in call:
            if (!isInCall() ){
                // - if we are docked to a BT CAR dock, give A2DP preference over earpiece
                // - if we are docked to a BT DESK dock, give speaker preference over earpiece
                if (mForceUse[AudioSystem::FOR_DOCK] == AudioSystem::FORCE_BT_CAR_DOCK) {
                    device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_BLUETOOTH_A2DP;
                } else if (mForceUse[AudioSystem::FOR_DOCK] == AudioSystem::FORCE_BT_DESK_DOCK) {
                    device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_SPEAKER;
                }
                if (device) break;
                // - phone strategy should route STREAM_VOICE_CALL to A2DP
                device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_BLUETOOTH_A2DP;
                if (device) break;
                device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_BLUETOOTH_A2DP_HEADPHONES;
                if (device) break;
            }
            if (mPhoneState == AudioSystem::MODE_RINGTONE)
                device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_SPEAKER;
            if (device) break;

            device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_EARPIECE;
            if (device == 0) {
                ALOGE("getDeviceForStrategy() earpiece device not found");
            }
            break;

        case AudioSystem::FORCE_SPEAKER:
            if (!isInCall() || strategy != STRATEGY_DTMF) {
                device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_BLUETOOTH_SCO_CARKIT;
                if (device) break;
            }
            // when not in call:
            if (!isInCall()) {
                // - if we are docked to a BT CAR dock, give A2DP preference over phone spkr
                if (mForceUse[AudioSystem::FOR_DOCK] == AudioSystem::FORCE_BT_CAR_DOCK) {
                    device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_BLUETOOTH_A2DP;
                    if (device) break;
                }
                // - phone strategy should route STREAM_VOICE_CALL to A2DP speaker
                //   when forcing to speaker output
                device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_BLUETOOTH_A2DP_SPEAKER;
                if (device) break;
            }
            device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_SPEAKER;
            if (device == 0) {
                ALOGE("getDeviceForStrategy() speaker device not found");
            }
            break;
        }
    break;

    case STRATEGY_SONIFICATION:

        // If incall, just select the STRATEGY_PHONE device: The rest of the behavior is handled by
        // handleIncallSonification().
        if (isInCall()) {
            device = getDeviceForStrategy(STRATEGY_PHONE, false /*fromCache*/);
            break;
        }
        // If not incall:
        // - if we are docked to a BT CAR dock, don't duplicate for the sonification strategy
        // - if we are docked to a BT DESK dock, use only speaker for the sonification strategy
        if (mForceUse[AudioSystem::FOR_DOCK] != AudioSystem::FORCE_BT_CAR_DOCK) {
            device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_SPEAKER;
            if (device == 0) {
                ALOGE("getDeviceForStrategy() speaker device not found");
            }
            if (mForceUse[AudioSystem::FOR_DOCK] == AudioSystem::FORCE_BT_DESK_DOCK) {
                if (mAvailableOutputDevices & AudioSystem::DEVICE_OUT_WIRED_HEADPHONE) {
                    device |= AudioSystem::DEVICE_OUT_WIRED_HEADPHONE;
                } else if (mAvailableOutputDevices & AudioSystem::DEVICE_OUT_WIRED_HEADSET) {
                    device |= AudioSystem::DEVICE_OUT_WIRED_HEADSET;
                }
                break;
            }
        } else {
            device = 0;
        }
        // The second device used for sonification is the same as the device used by media strategy
        // Note that when docked, we pick the device below (no duplication)
        // FALL THROUGH

    case STRATEGY_ENFORCED_AUDIBLE:
        // strategy STRATEGY_ENFORCED_AUDIBLE uses same routing policy as STRATEGY_SONIFICATION
        // except:
        //   - when in call where it doesn't default to STRATEGY_PHONE behavior
        //   - in countries where not enforced in which case it follows STRATEGY_MEDIA

        if (strategy == STRATEGY_SONIFICATION ||
                !mStreams[AUDIO_STREAM_ENFORCED_AUDIBLE].mCanBeMuted) {
            device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_SPEAKER;
            if (device == 0) {
                ALOGE("getDeviceForStrategy() speaker device not found for STRATEGY_SONIFICATION");
            }
        }

    case STRATEGY_MEDIA: {
        uint32_t device2 = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_AUX_DIGITAL;
#ifdef WITH_A2DP
        if (mHasA2dp && (mForceUse[AudioSystem::FOR_MEDIA] != AudioSystem::FORCE_NO_BT_A2DP) &&
                (getA2dpOutput() != 0) && !mA2dpSuspended) {
            if (device2 == 0) {
                // play ringtone over speaker (or speaker + headset) if in car dock
                // because A2DP is suspended in this case
                if (mForceUse[AudioSystem::FOR_DOCK] == AudioSystem::FORCE_BT_CAR_DOCK &&
                    strategy == STRATEGY_SONIFICATION &&
                    mPhoneState == AudioSystem::MODE_RINGTONE) {
                    device2 = mAvailableOutputDevices &
                              (AudioSystem::DEVICE_OUT_SPEAKER |
                               AudioSystem::DEVICE_OUT_WIRED_HEADPHONE |
                               AudioSystem::DEVICE_OUT_WIRED_HEADSET);
                }
            }
        }
#endif
        if (device2 == 0) {
            device2 = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_WIRED_HEADPHONE;
        }
        if (device2 == 0) {
            device2 = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_WIRED_HEADSET;
        }
#ifdef WITH_A2DP
        if (mHasA2dp && (mForceUse[AudioSystem::FOR_MEDIA] != AudioSystem::FORCE_NO_BT_A2DP) &&
                (getA2dpOutput() != 0) && !mA2dpSuspended) {
            if (device2 == 0) {
                device2 = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_BLUETOOTH_A2DP;
            }
            if (device2 == 0) {
                device2 = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_BLUETOOTH_A2DP_HEADPHONES;
            }
            if (device2 == 0) {
                device2 = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_BLUETOOTH_A2DP_SPEAKER;
            }
        }
#endif
        if (device2 == 0) {
            device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_SPEAKER;
        }
        // device is DEVICE_OUT_SPEAKER if we come from case STRATEGY_SONIFICATION, 0 otherwise
        device |= device2;
        if (device == 0) {
            ALOGE("getDeviceForStrategy() speaker device not found");
        }
        // Do not play media stream if in call and the requested device would change the hardware
        // output routing
        if (isInCall() &&
            !AudioSystem::isA2dpDevice((AudioSystem::audio_devices)device) &&
            device != getDeviceForStrategy(STRATEGY_PHONE)) {
            device = 0;
            ALOGV("getDeviceForStrategy() incompatible media and phone devices");
        }
        } break;

    default:
        ALOGW("getDeviceForStrategy() unknown strategy: %d", strategy);
        break;
    }

    ALOGV("getDeviceForStrategy() strategy %d, device %x", strategy, device);
    return (audio_devices_t)device;
}

float AudioPolicyManager::computeVolume(int stream, int index, audio_io_handle_t output, audio_devices_t device)
{
    // if requested volume index is the minimum possible value, we must honor this value as this
    // means the stream is muted. This overrides condition-specific modifications to the volume
    // computed in the generic APM
    if (index == mStreams[stream].mIndexMin) {
        return AudioPolicyManagerBase::computeVolume(stream, index, output, device);
    }

    // force volume on A2DP output to maximum if playing through car dock speakers
    // as volume is applied on the car dock and controlled via car dock keys.
#ifdef WITH_A2DP
    if (output == getA2dpOutput() &&
        mForceUse[AudioSystem::FOR_DOCK] == AudioSystem::FORCE_BT_CAR_DOCK) {
        return 1.0;
    }
#endif

    float volume = AudioPolicyManagerBase::computeVolume(stream, index, output, device);

    // limit stream volume when in call and playing over bluetooth SCO device to
    // avoid saturation
    if (mPhoneState == AudioSystem::MODE_IN_CALL && AudioSystem::isBluetoothScoDevice((AudioSystem::audio_devices)device)) {
        if (volume > IN_CALL_SCO_VOLUME_MAX) {
            ALOGV("computeVolume limiting SYSTEM volume %f to %f",volume, IN_CALL_SCO_VOLUME_MAX);
            volume = IN_CALL_SCO_VOLUME_MAX;
        }
    }

    // in car dock: when using the 3.5mm jack to play media, set a minimum volume as access to the
    // physical volume keys is blocked by the car dock frame.
    if ((mForceUse[AudioSystem::FOR_DOCK] == AudioSystem::FORCE_BT_CAR_DOCK) &&
            (volume < CAR_DOCK_MUSIC_MINI_JACK_VOLUME_MIN) &&
            (stream == AudioSystem::MUSIC) &&
            (device & (AudioSystem::DEVICE_OUT_WIRED_HEADPHONE |
                AudioSystem::DEVICE_OUT_WIRED_HEADSET))) {
        volume = CAR_DOCK_MUSIC_MINI_JACK_VOLUME_MIN;
    }

    return volume;
}




}; // namespace android
