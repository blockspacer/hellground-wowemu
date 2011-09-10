/*
 * Copyright (C) 2005-2008 MaNGOS <http://www.mangosproject.org/>
 *
 * Copyright (C) 2008 Trinity <http://www.trinitycore.org/>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#ifndef TRINITY_TIMER_H
#define TRINITY_TIMER_H

#include "Common.h"
#include <ace/OS_NS_sys_time.h>

class WorldTimer
{
        public:
        // Get current server time
        static uint32 getMSTime();
        // Get time difference between two timestamps 
        static inline uint32 getMSTimeDiff(const uint32& oldMSTime, const uint32& newMSTime)
        {
            if (oldMSTime > newMSTime)
            {
                const uint32 diff_1 = (uint32(0xFFFFFFFF) - oldMSTime) + newMSTime;
                const uint32 diff_2 = oldMSTime - newMSTime;

                return std::min(diff_1, diff_2);
            }

            return newMSTime - oldMSTime;
        }

        // Get time difference between oldMSTime and current server time 
        static inline uint32 getMSTimeDiffToNow(const uint32& oldMSTime)
        {
            return getMSTimeDiff(oldMSTime, WorldTimer::getMSTime());
        }

        // Get last world tick time
        static uint32 tickTime();
        // Get previous world tick time
        static uint32 tickPrevTime();
        // Tick world timer
        static uint32 tick();

    private:
        WorldTimer();
        WorldTimer(const WorldTimer& );

        // Analogue to WorldTimer::getMSTime() but it persists m_SystemTickTime
        static uint32 getMSTime_internal(bool savetime = false);

        static uint32 m_iTime;
        static uint32 m_iPrevTime;
};

class IntervalTimer
{
    public:
        IntervalTimer() : _interval(0), _current(0) {}

        void Update(time_t diff) { _current += diff; if(_current<0) _current=0;}
        bool Passed() { return _current >= _interval; }
        void Reset() { if(_current >= _interval) _current -= _interval;  }

        void SetCurrent(time_t current) { _current = current; }
        void SetInterval(time_t interval) { _interval = interval; }
        time_t GetInterval() const { return _interval; }
        time_t GetCurrent() const { return _current; }

    private:
        time_t _interval;
        time_t _current;
};

struct TimeTracker
{
    TimeTracker(time_t expiry) : i_expiryTime(expiry) {}
    void Update(time_t diff) { i_expiryTime -= diff; }
    bool Passed(void) const { return (i_expiryTime <= 0); }
    void Reset(time_t interval) { i_expiryTime = interval; }
    time_t GetExpiry(void) const { return i_expiryTime; }
    time_t i_expiryTime;
};

struct TimeTrackerSmall
{
    TimeTrackerSmall(int32 expiry) : i_expiryTime(expiry) {}
    void Update(int32 diff) { i_expiryTime -= diff; }
    bool Passed(void) const { return (i_expiryTime <= 0); }
    void Reset(int32 interval) { i_expiryTime = interval; }
    int32 GetExpiry(void) const { return i_expiryTime; }
    int32 i_expiryTime;
};

struct PeriodicTimer
{
    public:

        PeriodicTimer(int32 period, int32 start_time)
            : i_expireTime(start_time), i_period(period)
        {
        }

        bool Update(const uint32 &diff)
        {
            if ((i_expireTime -= diff) > 0)
                return false;

            i_expireTime += i_period > diff ? i_period : diff;
            return true;
        }

        void SetPeriodic(int32 period, int32 start_time)
        {
            i_expireTime = start_time;
            i_period = period;
        }

        // Tracker interface
        void TUpdate(int32 diff) { i_expireTime -= diff; }
        bool TPassed() const { return i_expireTime <= 0; }
        void TReset(int32 diff, int32 period)  { i_expireTime += period > diff ? period : diff; }

    private:

        int32 i_period;
        int32 i_expireTime;
};

struct ShortTimeTracker
{
    public:
        ShortTimeTracker(int32 expiry = 0) : i_expiryTime(expiry) {}
        void Update(int32 diff) { i_expiryTime -= diff; }
        bool Passed() const { return (i_expiryTime <= 0); }
        void Reset(int32 interval) { i_expiryTime = interval; }
        int32 GetExpiry() const { return i_expiryTime; }

    private:
        int32 i_expiryTime;
};

#endif
