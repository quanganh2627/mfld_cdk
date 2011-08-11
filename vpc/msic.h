/*
 **
 ** Copyright 2011 Intel Corporation
 **
 ** Licensed under the Apache License, Version 2.0 (the "License");
 ** you may not use this file except in compliance with the License.
 ** You may obtain a copy of the License at
 **
 **      http://www.apache.org/licenses/LICENSE-2.0
 **
 ** Unless required by applicable law or agreed to in writing, software
 ** distributed under the License is distributed on an "AS IS" BASIS,
 ** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 ** See the License for the specific language governing permissions and
 ** limitations under the License.
 */

#include <alsa/asoundlib.h>
#include <alsa/control_external.h>

namespace android
{

class msic
{
public :
    static int pcm_init();
    static int pcm_enable();
    static int pcm_disable();

private :
    static snd_pcm_t *handle_playback;
    static snd_pcm_t *handle_capture;
};

}

