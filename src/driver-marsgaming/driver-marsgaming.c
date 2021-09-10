#include "libratbag-private.h"
#include "marsgaming-probe.h"
#include "marsgaming-commit.h"

static void
marsgaming_remove(struct ratbag_device *device)
{
	struct ratbag_profile *profile;
	ratbag_device_for_each_profile(device, profile) {
		free(profile->drv_data);
	}
}

struct ratbag_driver marsgaming_driver = {
	.name = "Mars Gaming",
	.id = "marsgaming",
	.probe = marsgaming_probe,
	.commit = marsgaming_commit,
	.remove = marsgaming_remove,
	.set_active_profile = marsgaming_set_active_profile
};
