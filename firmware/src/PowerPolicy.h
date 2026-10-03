#pragma once
#include <stdint.h>

// When the box puts itself to sleep, decided from the clock and the cell alone
// so it can be tested on the host.
class PowerPolicy {
public:
	enum class Verdict : uint8_t { Awake, Idle, Flat };

	// idleMs 0 never sleeps for idleness; a flat cell still sleeps.
	PowerPolicy(uint32_t idleMs, uint16_t criticalMv, uint32_t criticalHoldMs)
	    : _idleMs(idleMs), _criticalMv(criticalMv), _holdMs(criticalHoldMs) {}

	void setIdleMs(uint32_t ms) { _idleMs = ms; }
	uint32_t idleMs() const { return _idleMs; }

	void activity(uint32_t now) { _lastActivity = now; }

	// The EMA already smooths the ADC; the hold rides out a volume peak that
	// sags the cell for a few seconds.
	Verdict check(uint32_t now, bool battPresent, uint16_t mv) {
		if (flat(battPresent, mv)) {
			if (!_low) {
				_low = true;
				_lowSince = now;
			} else if (now - _lowSince >= _holdMs) {
				return Verdict::Flat;
			}
		} else {
			_low = false;
		}
		if (_idleMs && now - _lastActivity >= _idleMs) return Verdict::Idle;
		return Verdict::Awake;
	}

	// At boot nothing draws yet, so one reading under the line is enough.
	bool flat(bool battPresent, uint16_t mv) const { return battPresent && mv && mv < _criticalMv; }

private:
	uint32_t _idleMs;
	uint16_t _criticalMv;
	uint32_t _holdMs;
	uint32_t _lastActivity = 0;
	uint32_t _lowSince = 0;
	bool _low = false;
};
