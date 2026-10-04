#pragma once
#include <stdint.h>

// When the box puts itself to sleep, decided from the clock and the cell alone
// so it can be tested on the host.
class PowerPolicy {
public:
	enum class Verdict : uint8_t { Awake, Idle, Flat };

	// idleMs 0 never sleeps for idleness; a flat cell still sleeps.
	PowerPolicy(uint32_t idleMs, uint16_t criticalMv, uint32_t criticalHoldMs, uint16_t chargeRiseMv)
	    : _idleMs(idleMs), _criticalMv(criticalMv), _holdMs(criticalHoldMs), _riseMv(chargeRiseMv) {}

	void setIdleMs(uint32_t ms) { _idleMs = ms; }
	uint32_t idleMs() const { return _idleMs; }

	void activity(uint32_t now) { _lastActivity = now; }

	// The hold rides out a volume peak that sags the cell. A reading climbing
	// back is a charger the box cannot otherwise see: the hold starts over.
	Verdict check(uint32_t now, bool battPresent, uint16_t mv) {
		if (flat(battPresent, mv)) {
			if (!_low || mv >= _lowest + _riseMv) {
				_low = true;
				_lowSince = now;
				_lowest = mv;
			} else {
				if (mv < _lowest) _lowest = mv;
				if (now - _lowSince >= _holdMs) return Verdict::Flat;
			}
		} else {
			_low = false;
		}
		if (_idleMs && now - _lastActivity >= _idleMs) return Verdict::Idle;
		return Verdict::Awake;
	}

	bool flat(bool battPresent, uint16_t mv) const { return battPresent && mv && mv < _criticalMv; }

	// Only after a wake from sleep: switching on or plugging in may be the fix.
	bool sleepAtBoot(bool wokeFromSleep, bool battPresent, uint16_t mv) const {
		return wokeFromSleep && flat(battPresent, mv);
	}

private:
	uint32_t _idleMs;
	uint16_t _criticalMv;
	uint32_t _holdMs;
	uint16_t _riseMv;
	uint16_t _lowest = 0;
	uint32_t _lastActivity = 0;
	uint32_t _lowSince = 0;
	bool _low = false;
};
