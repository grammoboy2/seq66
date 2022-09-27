/*
 *  This file is part of seq66.
 *
 *  seq66 is free software; you can redistribute it and/or modify it under the
 *  terms of the GNU General Public License as published by the Free Software
 *  Foundation; either version 2 of the License, or (at your option) any later
 *  version.
 *
 *  seq66 is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with seq66; if not, write to the Free Software Foundation, Inc., 59 Temple
 *  Place, Suite 330, Boston, MA  02111-1307  USA
 */

/**
 * \file          midi_jack_data.cpp
 *
 *    Object for holding the current status of JACK and JACK MIDI data.
 *
 * \library       seq66 application
 * \author        Chris Ahlstrom
 * \date          2022-09-13
 * \updates       2022-09-27
 * \license       See above.
 *
 *  GitHub issue #165: enabled a build and run with no JACK support.
 */

#include "midi_jack_data.hpp"           /* seq66::midi_jack_data class      */

#if defined SEQ66_JACK_SUPPORT

#if defined SEQ66_PLATFORM_DEBUG_TMI
#include "midi/calculations.hpp"        /* srq66::pulse_length_us()         */
#endif

/*
 * Do not document the namespace; it breaks Doxygen.
 */

namespace seq66
{

/**
 *  Static members used for calculating frame offsets in the Seq66 JACK
 *  output callback.
 */

jack_nframes_t midi_jack_data::sm_jack_frame_rate   = 0;
double midi_jack_data::sm_jack_ticks_per_beat       = 1920.0;
double midi_jack_data::sm_jack_beats_per_minute     = 120.0;
double midi_jack_data::sm_jack_frame_factor         = 1.0;

/**
 * \ctor midi_jack_data
 */

midi_jack_data::midi_jack_data () :
    m_jack_client           (nullptr),
    m_jack_port             (nullptr),
#if defined SEQ66_USE_MIDI_MESSAGE_RINGBUFFER
    m_jack_buffer           (nullptr),      /* ring_buffer<midi_message>    */
#else
    m_jack_buffmessage      (nullptr),
#endif
    m_jack_lasttime         (0),
#if defined SEQ66_MIDI_PORT_REFRESH
    m_internal_port_id      (null_system_port_id()),
#endif
    m_jack_rtmidiin         (nullptr)
{
    // Empty body
}

/**
 *  This destructor currently does nothing.  We rely on the enclosing class
 *  to close out the things that it created.
 */

midi_jack_data::~midi_jack_data ()
{
    // Empty body
}

/**
 *  Emobdies the calculation of the pulse factor reasoned out in the
 *  jack_frame_offset() function below.
 *
 * TODO:
 *
 *      -   The jack_position_t's ticks_per_beat and beats_per_minute are
 *          currently zero if seq66 is not transport (slave or master).  Then
 *          we need to make sure the beats-per-minute is set from the
 *          tune settings, and the ticks-per-beat is set to PPQN * 10.
 *      -   It would be good to call this function during a settings change as
 *          indicated by calls to the jack_set_buffer_size(frames, arg) or
 *          jack_set_sample_rate(frames, arg) callbacks.
 */

bool
midi_jack_data::recalculate_frame_factor (const jack_position_t & pos)
{
    bool changed = false;
    if
    (
        (pos.ticks_per_beat > 1.0) &&                       /* sanity check */
        (jack_ticks_per_beat() != pos.ticks_per_beat)
    )
    {
        jack_ticks_per_beat(pos.ticks_per_beat);
        changed = true;
    }
    if
    (
        (pos.beats_per_minute > 1.0) &&                     /* sanity check */
        (jack_beats_per_minute() != pos.beats_per_minute)
    )
    {
        jack_beats_per_minute(pos.beats_per_minute);
        changed = true;
    }
    if (jack_frame_rate() != pos.frame_rate)
    {
        jack_frame_rate(pos.frame_rate);
        changed = true;
    }
    if (changed)
    {
        /*
         * This is the ALSA period, said not to matter but it does in some
         * enviroments.  Weird.  If we prove that, we will have deal with it.
         */

        const double bpmtbpfactor = 600.0;         /* no dividing by "periods" */
        sm_jack_frame_factor = (jack_frame_rate() * bpmtbpfactor) /
            (jack_ticks_per_beat() * jack_beats_per_minute());
#if defined SEQ66_PLATFORM_DEBUG_TMI
        double pl = pulse_length_us
        (
            jack_beats_per_minute(), 0.1 * jack_ticks_per_beat()
        );
        printf("[debug] frames/tick = %g; ticks/beat = %g; pulse = %g us\n",
            sm_jack_frame_factor, jack_ticks_per_beat(), pl);
#endif
    }
    return changed;
}

/**
 *  Attempts to convert ticks to a frame offset (0 to framect).
 *
 * The problem (issue #100):
 *
 *  Our RtMidi-based process handles output by first pushing the current
 *  playback tick (pulse) and the message bytes to the JACK ringubffer.
 *  This happens at frame f0 and time t1.  In the JACK output callback,
 *  the message data is plucked from the ringbuffer at later frame f1 and time
 *  t1.  There, we need an offset from the current frame to the actual frame, so
 *  that the messages don't jitter to much with large frame sizes.
 *
 * The solution:
 *
 *  -#  We assume that the tick position p is in the same relative location
 *      relative to the current frame when placed into the ringbuffer and when
 *      retrieved from the ring buffer.
 *  -#  We can use the calculations modules pulse_length_us() function to
 *      get the length of a tick in seconds:  60 / ppqn / bpm. Therefore the
 *      tick time t(p) = p * 60 / ppqn / bpm.
 *  -#  What is the number of frames at at given time?  The duration of each
 *      frame is D = 1 / R. So the time at the end of frame Fn is
 *      T(n) = n / R.
 *
 *  The latency L of a frame is given by the number of frames, F, the sampling
 *  rate R (e.g. 48000), and the number of periods P: L = P * F / R.  Here is a
 *  brief table for 48000, 2 periods:
 *
\verbatim
        Frames      Latency     Qjackctl P=2    Qjackctl P-3
          32        0.667 ms    (doubles them)  (triples them)
          64        1.333 ms
         128        2.667 ms
         256        5.333 ms
         512       10.667 ms
        1024       21.333 ms
        2048       42.667 ms
        4096       85.333 ms
\endverbatim
 *
 *  The jackd arguments from its man apge:
 *
 *      -   --rate:     The sample rate, R.  Defaults 48000.
 *      -   --period:   The number of frames between the process callback
 *                      calls, F.  Must be a power of 2, defaults to 1024. Set
 *                      as low as possible to avoid xruns.
 *      -   --nperiods: The number of periods of playback latency, P. Defaults
 *                      to the minimum, 2.  For USB audio, 3 is recommended.
 *                      This an ALSA engine parameter, but it might contribute
 *                      to the duration of a process callback cycle [proved
 *                      using probe code to calculate microseconds.]
 *
 *  So, given a tick position p, what is the frame offset needed?
 *
\verbatim
        t(p) = 60 * p / ppqn / bpm, but ppqn = ticks_per_beat / 10, so
        t(p) = 60 * p / (Tpb / 10) / bpm
        T(n) = n * P * F / R
        60 * 10 * p / (Tpb * bpm) = n * P * F / R
        n = 60 * 10 * p * R / (Tpb * bpm * P * F)

                                seconds    1     beats    minutes
        frame = pulse * 10 * 60 ------- * ---- * ------ * -------
                                minute    Tpb    ticks     beat
\endverbatim
 *
 * Typical values:
 *
\verbatim
        frame_rate          = 48000
        beats_per_bar       = 4
        beat_type           = 4
        ticks_per_beat      = 1920
        beats_per_minute    = 120
\endverbatim
 *
 * \param F
 *      The current frame count (the jack_nframes framect) parameter to the
 *      callback.
 *
 * \param p
 *      The running timestamp, in pulses, of the event being emitted.
 *
 * \return
 *      Returns a putative offset value to use in reserving the event.
 */

jack_nframes_t
midi_jack_data::jack_frame_offset (jack_nframes_t F, midipulse p)
{
    jack_nframes_t result = jack_frame_estimate(p);
#if defined SEQ66_PLATFORM_DEBUG_TMI
    double temp = result;
#endif
    if (F > 1)
        result = result % F;

#if defined SEQ66_PLATFORM_DEBUG_TMI
    printf("[debug] ts %ld * %g --> frame %g; offset %u\n",
        long(p), jack_frame_factor(), temp, unsigned(result));
#endif

    return result;
}

jack_nframes_t
midi_jack_data::jack_frame_estimate (midipulse p)
{
    double temp = p * jack_frame_factor();
    return jack_nframes_t(temp);
}

/**
 *  Calculates the putative cycle number, without truncation, because we want
 *  the fractional part.
 *
 * \param f
 *      The current frame number.
 *
 * \param F
 *      The number of frames in a cycle.  (A power of 2.)
 *
 * \return
 *      The cycle number with fractional portion.
 */

double
midi_jack_data::cycle (jack_nframes_t f, jack_nframes_t F)
{
    return double(f) / double(F);
}

double
midi_jack_data::pulse_cycle (midipulse p, jack_nframes_t F)
{
    return p * jack_frame_factor() / double(F);
}

}           // namespace seq66

#endif      // SEQ66_JACK_SUPPORT

/*
 * midi_jack_data.cpp
 *
 * vim: sw=4 ts=4 wm=4 et ft=cpp
 */

