/* Light Gun XR - Light Gun Simulator for OpenXR and uinput
 *
 * Copyright (c) 2024 Ethan Lee
 *
 * This software is provided 'as-is', without any express or implied warranty.
 * In no event will the authors be held liable for any damages arising from
 * the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 * claim that you wrote the original software. If you use this software in a
 * product, an acknowledgment in the product documentation would be
 * appreciated but is not required.
 *
 * 2. Altered source versions must be plainly marked as such, and must not be
 * misrepresented as being the original software.
 *
 * 3. This notice may not be removed or altered from any source distribution.
 *
 * Ethan "flibitijibibo" Lee <flibitijibibo@flibitijibibo.com>
 *
 */

/* Instructions:
 * TODO: Setup process for screen rectangle calibration
 *
 * Don't forget to link SteamVR as the active OpenXR runtime!
 * ln -sf ~/.steam/steam/steamapps/common/SteamVR/steamxr_linux64.json ~/.config/openxr/1/active_runtime.json
 */

#include <openxr/openxr.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
	XrInstance instance;
	XrInstanceCreateInfo instanceCreateInfo;
	XrResult res;
	char resString[XR_MAX_RESULT_STRING_SIZE];

	instanceCreateInfo.type = XR_TYPE_INSTANCE_CREATE_INFO;
	instanceCreateInfo.next = NULL;
	instanceCreateInfo.createFlags = 0;
	strncpy(instanceCreateInfo.applicationInfo.applicationName, "Light Gun XR", XR_MAX_APPLICATION_NAME_SIZE);
	instanceCreateInfo.applicationInfo.applicationVersion = 0;
	strncpy(instanceCreateInfo.applicationInfo.engineName, "Light Gun XR", XR_MAX_ENGINE_NAME_SIZE);
	instanceCreateInfo.applicationInfo.engineVersion = 0;
	instanceCreateInfo.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
	instanceCreateInfo.enabledApiLayerCount = 0;
	instanceCreateInfo.enabledApiLayerNames = NULL;
	instanceCreateInfo.enabledExtensionCount = 0;
	instanceCreateInfo.enabledExtensionNames = NULL;

	res = xrCreateInstance(&instanceCreateInfo, &instance);
	if (res != XR_SUCCESS)
	{
		/* Have to do this manually without an instance... */
		switch (res)
		{
			#define TOSTR(a) case a: strncpy(resString, #a, sizeof(resString)); break;
			TOSTR(XR_ERROR_VALIDATION_FAILURE)
			TOSTR(XR_ERROR_RUNTIME_FAILURE)
			TOSTR(XR_ERROR_OUT_OF_MEMORY)
			TOSTR(XR_ERROR_LIMIT_REACHED)
			TOSTR(XR_ERROR_RUNTIME_UNAVAILABLE)
			TOSTR(XR_ERROR_NAME_INVALID)
			TOSTR(XR_ERROR_INITIALIZATION_FAILED)
			TOSTR(XR_ERROR_EXTENSION_NOT_PRESENT)
			TOSTR(XR_ERROR_API_VERSION_UNSUPPORTED)
			TOSTR(XR_ERROR_API_LAYER_NOT_PRESENT)
			#undef TOSTR
			default: strncpy(resString, "UNKNOWN", sizeof(resString)); break;
		}
		printf("xrCreateInstance: %s\n", resString);
		return -1;
	}

	/* TODO: Draw the rest of the owl */

	xrDestroyInstance(instance);

	return 0;
}
