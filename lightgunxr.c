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
#include <stdio.h> /* printf */
#include <string.h> /* strncpy */
#include <unistd.h> /* usleep */

int main(int argc, char **argv)
{
	/* "Global" variables */

	XrInstance instance = 0;
	XrActionSet actionSet = 0;
	XrAction
		aim = 0,
		fire = 0,
		pedal = 0,
		pause = 0,
		kickback = 0;
	XrSession session = 0;

	/* Error handling */

	XrResult res;
	char resString[XR_MAX_RESULT_STRING_SIZE];

	/* Instance creation */

	XrInstanceCreateInfo instanceCreateInfo;
	const char *extension = XR_MND_HEADLESS_EXTENSION_NAME;

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
	instanceCreateInfo.enabledExtensionCount = 1;
	instanceCreateInfo.enabledExtensionNames = &extension;

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

	/* Error handling */

	int returnCode = -1;

	#define CHECK_ERROR(name) \
		if (res != XR_SUCCESS) \
		{ \
			xrResultToString(instance, res, resString); \
			printf(#name ": %s\n", resString); \
			goto cleanup; \
		}

	/* Action set */

	returnCode = -2;

	XrActionSetCreateInfo actionsetCreateInfo;
	XrActionCreateInfo actionCreateInfo;

	actionsetCreateInfo.type = XR_TYPE_ACTION_SET_CREATE_INFO;
	actionsetCreateInfo.next = NULL;
	strncpy(actionsetCreateInfo.actionSetName, "lightgun", XR_MAX_ACTION_SET_NAME_SIZE);
	strncpy(actionsetCreateInfo.localizedActionSetName, "Light Gun", XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE);
	actionsetCreateInfo.priority = 0;

	res = xrCreateActionSet(instance, &actionsetCreateInfo, &actionSet);
	CHECK_ERROR(xrCreateActionSet)

	actionCreateInfo.type = XR_TYPE_ACTION_CREATE_INFO;
	actionCreateInfo.next = NULL;
	actionCreateInfo.countSubactionPaths = 0;
	actionCreateInfo.subactionPaths = NULL;

	#define SETUP_ACTION(name, localized, type) \
		strncpy(actionCreateInfo.actionName, #name, XR_MAX_ACTION_NAME_SIZE); \
		strncpy(actionCreateInfo.localizedActionName, localized, XR_MAX_LOCALIZED_ACTION_NAME_SIZE); \
		actionCreateInfo.actionType = type; \
		res = xrCreateAction(actionSet, &actionCreateInfo, &name); \
		CHECK_ERROR(xrCreateAction)

	SETUP_ACTION(aim, "Aim", XR_ACTION_TYPE_POSE_INPUT)
	SETUP_ACTION(fire, "Fire", XR_ACTION_TYPE_BOOLEAN_INPUT)
	SETUP_ACTION(pedal, "Pedal", XR_ACTION_TYPE_BOOLEAN_INPUT)
	SETUP_ACTION(pause, "Pause", XR_ACTION_TYPE_BOOLEAN_INPUT)
	SETUP_ACTION(kickback, "Kickback", XR_ACTION_TYPE_VIBRATION_OUTPUT)

	#undef SETUP_ACTION

	/* Bind to Valve Index */

	XrPath indexPath;
	XrActionSuggestedBinding indexBindings[6];
	XrInteractionProfileSuggestedBinding indexBindingCreateInfo;

	res = xrStringToPath(instance, "/interaction_profiles/valve/index_controller", &indexPath);
	CHECK_ERROR(xrStringToPath)

	indexBindings[0].action = aim;
	res = xrStringToPath(instance, "/user/hand/right/input/aim/pose", &indexBindings[0].binding);
	CHECK_ERROR(xrStringToPath)

	indexBindings[1].action = fire;
	res = xrStringToPath(instance, "/user/hand/right/input/trigger/click", &indexBindings[1].binding);
	CHECK_ERROR(xrStringToPath)

	indexBindings[2].action = pedal;
	res = xrStringToPath(instance, "/user/hand/right/input/a/click", &indexBindings[2].binding);
	CHECK_ERROR(xrStringToPath)

	indexBindings[3].action = pedal;
	res = xrStringToPath(instance, "/user/hand/right/input/b/click", &indexBindings[3].binding);
	CHECK_ERROR(xrStringToPath)

	indexBindings[4].action = pause;
	res = xrStringToPath(instance, "/user/hand/right/input/thumbstick/click", &indexBindings[4].binding);
	CHECK_ERROR(xrStringToPath)

	indexBindings[5].action = kickback;
	res = xrStringToPath(instance, "/user/hand/right/output/haptic", &indexBindings[5].binding);
	CHECK_ERROR(xrStringToPath)

	indexBindingCreateInfo.type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING;
	indexBindingCreateInfo.next = NULL;
	indexBindingCreateInfo.interactionProfile = indexPath;
	indexBindingCreateInfo.countSuggestedBindings = 6;
	indexBindingCreateInfo.suggestedBindings = indexBindings;

	res = xrSuggestInteractionProfileBindings(instance, &indexBindingCreateInfo);
	CHECK_ERROR(xrSuggestInteractionProfileBindings)

	/* Session creation */

	returnCode = -3;

	XrSystemId systemID;
	XrSystemGetInfo systemGetInfo;
	XrSessionCreateInfo sessionCreateInfo;
	XrSessionActionSetsAttachInfo attachInfo;

	systemGetInfo.type = XR_TYPE_SYSTEM_GET_INFO;
	systemGetInfo.next = NULL;
	systemGetInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

	res = xrGetSystem(instance, &systemGetInfo, &systemID);
	CHECK_ERROR(xrGetSystem)

	sessionCreateInfo.type = XR_TYPE_SESSION_CREATE_INFO;
	sessionCreateInfo.next = NULL; /* XR_MND_headless enables this to be NULL */
	sessionCreateInfo.createFlags = 0;
	sessionCreateInfo.systemId = systemID;

	res = xrCreateSession(instance, &sessionCreateInfo, &session);
	CHECK_ERROR(xrCreateSession)

	attachInfo.type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO;
	attachInfo.next = NULL;
	attachInfo.countActionSets = 1;
	attachInfo.actionSets = &actionSet;

	res = xrAttachSessionActionSets(session, &attachInfo);
	CHECK_ERROR(xrAttachSessionActionSets)

	/* Wait for the signal to begin the session */

	returnCode = -4;

	XrEventDataBuffer eventData;
	XrSessionBeginInfo beginInfo;

	do
	{
		eventData.type = XR_TYPE_EVENT_DATA_BUFFER;
		eventData.next = NULL;

		res = xrPollEvent(instance, &eventData);
		CHECK_ERROR(xrPollEvent)
	} while (eventData.type != XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED);

	beginInfo.type = XR_TYPE_SESSION_BEGIN_INFO;
	beginInfo.next = NULL;
	beginInfo.primaryViewConfigurationType = 0; /* XR_MND_headless enables this to be 0 */

	res = xrBeginSession(session, &beginInfo);
	CHECK_ERROR(xrBeginSession)

	/* Action polling, finally. */

	returnCode = -5;

	int run = 1;
	XrActiveActionSet activeSet;
	XrActionsSyncInfo syncInfo;
	XrActionStateGetInfo getInfo;

	activeSet.actionSet = actionSet;
	activeSet.subactionPath = XR_NULL_PATH;

	syncInfo.type = XR_TYPE_ACTIONS_SYNC_INFO;
	syncInfo.next = NULL;
	syncInfo.countActiveActionSets = 1;
	syncInfo.activeActionSets = &activeSet;

	getInfo.type = XR_TYPE_ACTION_STATE_GET_INFO;
	getInfo.next = NULL;
	getInfo.subactionPath = XR_NULL_PATH;

	while (run)
	{
		res = xrSyncActions(session, &syncInfo);
		if (res == XR_SUCCESS)
		{
			XrActionStatePose aimState;
			XrActionStateBoolean fireState, pedalState, pauseState;

			getInfo.action = aim;
			res = xrGetActionStatePose(session, &getInfo, &aimState);
			CHECK_ERROR(xrGetActionStatePose)

			getInfo.action = fire;
			res = xrGetActionStateBoolean(session, &getInfo, &fireState);
			CHECK_ERROR(xrGetActionStateBoolean)

			getInfo.action = pedal;
			res = xrGetActionStateBoolean(session, &getInfo, &pedalState);
			CHECK_ERROR(xrGetActionStateBoolean)

			getInfo.action = pause;
			res = xrGetActionStateBoolean(session, &getInfo, &pauseState);
			CHECK_ERROR(xrGetActionStateBoolean)

			/* TODO: Haptic output */

			/* TODO: run = 0 when fire and pedal are held for 3 seconds */

#if 0
			printf(
				"State:\n"
				"\tAim: TODO\n"
				"\tFire: %d\n"
				"\tPedal: %d\n"
				"\tPause: %d\n",
				fireState.currentState,
				pedalState.currentState,
				pauseState.currentState
			);
#endif
		}
		else if (res == XR_SESSION_LOSS_PENDING)
		{
			printf("Session is getting lost, bailing\n");
			run = 0;
		}
		else if (res != XR_SESSION_NOT_FOCUSED)
		{
			CHECK_ERROR(xrSyncActions)
		}

		/* Per XR_MND_headless, we need to throttle our event loop */
		usleep(1000);
	}

	/* Clean up. We out. */
	returnCode = 0;
cleanup:
	xrEndSession(session);
	xrDestroySession(session);
	xrDestroyAction(aim);
	xrDestroyAction(fire);
	xrDestroyAction(pedal);
	xrDestroyAction(pause);
	xrDestroyAction(kickback);
	xrDestroyActionSet(actionSet);
	xrDestroyInstance(instance);
	return returnCode;
}
