#include "kifu-logo-state-machine.h"

#include <stdlib.h>

enum kifu_logo_state {
	KIFU_LOGO_STATE_WAIT_INTERVAL,
	KIFU_LOGO_STATE_WAIT_STABLE_TWO_DICE,
	KIFU_LOGO_STATE_SHOWING,
};

struct kifu_logo_state_machine {
	enum kifu_logo_state state;
	uint64_t state_since_ns;
	uint64_t show_started_ns;
	uint64_t boot_grace_until_ns;
	uint64_t two_dice_started_ns;
};

static const uint64_t KIFU_LOGO_IDLE_DELAY_NS = 3ULL * 60ULL * 1000000000ULL;
static const uint64_t KIFU_LOGO_TWO_DICE_STABLE_NS = 5ULL * 1000000000ULL;
static const uint64_t KIFU_LOGO_FADE_IN_NS = 500ULL * 1000000ULL;
static const uint64_t KIFU_LOGO_FULL_VISIBLE_NS = 3ULL * 1000000000ULL;
static const uint64_t KIFU_LOGO_FADE_OUT_NS = 500ULL * 1000000ULL;
static const uint64_t KIFU_LOGO_CYCLE_NS = 5ULL * 1000000000ULL;
static const uint64_t KIFU_LOGO_BOOT_GRACE_NS = 4ULL * 1000000000ULL;

static void set_state(struct kifu_logo_state_machine *machine, enum kifu_logo_state state, uint64_t now_ns)
{
	machine->state = state;
	machine->state_since_ns = now_ns;
}

static void start_showing(struct kifu_logo_state_machine *machine, uint64_t now_ns)
{
	set_state(machine, KIFU_LOGO_STATE_SHOWING, now_ns);
	machine->show_started_ns = now_ns;
	machine->two_dice_started_ns = 0U;
}

struct kifu_logo_state_machine *kifu_logo_state_machine_create(void)
{
	return calloc(1U, sizeof(struct kifu_logo_state_machine));
}

void kifu_logo_state_machine_destroy(struct kifu_logo_state_machine *machine)
{
	if (machine != NULL) {
		free(machine);
	}
}

void kifu_logo_state_machine_activate(struct kifu_logo_state_machine *machine, uint64_t now_ns)
{
	if (machine == NULL) {
		return;
	}

	machine->boot_grace_until_ns = now_ns + KIFU_LOGO_BOOT_GRACE_NS;
	start_showing(machine, now_ns);
}

bool kifu_logo_state_machine_update(struct kifu_logo_state_machine *machine, uint64_t now_ns, uint32_t latest_dice_count)
{
	if (machine == NULL) {
		return false;
	}

	if (machine->state_since_ns == 0U) {
		set_state(machine, KIFU_LOGO_STATE_WAIT_INTERVAL, now_ns);
	}

	if (machine->state == KIFU_LOGO_STATE_SHOWING) {
		if (machine->show_started_ns == 0U) {
			machine->show_started_ns = now_ns;
		}

		if (now_ns > machine->show_started_ns && now_ns - machine->show_started_ns >= KIFU_LOGO_CYCLE_NS) {
			machine->show_started_ns = 0U;
			set_state(machine, KIFU_LOGO_STATE_WAIT_INTERVAL, now_ns);
			return false;
		}
		return true;
	}

	if (machine->boot_grace_until_ns > now_ns) {
		start_showing(machine, now_ns);
		return true;
	}

	if (machine->state == KIFU_LOGO_STATE_WAIT_INTERVAL) {
		if (now_ns > machine->state_since_ns && now_ns - machine->state_since_ns >= KIFU_LOGO_IDLE_DELAY_NS) {
			set_state(machine, KIFU_LOGO_STATE_WAIT_STABLE_TWO_DICE, now_ns);
			machine->two_dice_started_ns = 0U;
		}
		return false;
	}

	if (machine->state == KIFU_LOGO_STATE_WAIT_STABLE_TWO_DICE) {
		/* latest_dice_count already includes short-gap compensation from dice stabilizer. */
		if (latest_dice_count == 2U) {
			if (machine->two_dice_started_ns == 0U) {
				machine->two_dice_started_ns = now_ns;
			} else if (now_ns > machine->two_dice_started_ns &&
				   now_ns - machine->two_dice_started_ns >= KIFU_LOGO_TWO_DICE_STABLE_NS) {
				start_showing(machine, now_ns);
				return true;
			}
		} else {
			machine->two_dice_started_ns = 0U;
		}
	}

	return false;
}

float kifu_logo_state_machine_opacity(const struct kifu_logo_state_machine *machine, uint64_t now_ns)
{
	if (machine == NULL || machine->state != KIFU_LOGO_STATE_SHOWING || machine->show_started_ns == 0U) {
		return 0.0F;
	}

	if (now_ns <= machine->show_started_ns) {
		return 0.0F;
	}

	const uint64_t elapsed_ns = now_ns - machine->show_started_ns;
	if (elapsed_ns < KIFU_LOGO_FADE_IN_NS) {
		return (float)elapsed_ns / (float)KIFU_LOGO_FADE_IN_NS;
	}
	if (elapsed_ns < (KIFU_LOGO_FADE_IN_NS + KIFU_LOGO_FULL_VISIBLE_NS)) {
		return 1.0F;
	}
	if (elapsed_ns < KIFU_LOGO_CYCLE_NS) {
		const uint64_t fade_out_elapsed_ns = elapsed_ns - (KIFU_LOGO_FADE_IN_NS + KIFU_LOGO_FULL_VISIBLE_NS);
		const float fade_out_progress = (float)fade_out_elapsed_ns / (float)KIFU_LOGO_FADE_OUT_NS;
		return 1.0F - fade_out_progress;
	}

	return 0.0F;
}
