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
#include <string.h> /* strncpy, memset */
#include <unistd.h> /* usleep */
#include <time.h> /* clock_gettime */
#include <math.h> /* fabsf, fmodf, sqrtf, powf, cosf, asinf, M_PI */

#define XR_USE_TIMESPEC
#include <openxr/openxr_platform.h> /* xrConvertTimespecTimeToTimeKHR */

#ifdef __linux__
#include <linux/uinput.h>
#include <errno.h>
#include <fcntl.h> /* open, write */
#else
#error Only Linux is supported!
#endif

#define SCREEN_WIDTH 1920
#define SCREEN_HEIGHT 1080

/* Given a pose with position/orientation and a rect defined by four 3D points,
 * attempts to find where a ray casted by the pose intersects with the rect,
 * then normalizes the result.
 *
 * For example, a pose pointing directly at the center of the rectangle will
 * evaluate to [0.5, 0.5].
 *
 * When the ray does NOT point at the rectangle (i.e. it's parallel to or facing
 * away from it) the result is discarded entirely.
 *
 * When the result is valid AND newer than the current values of mouseX/mouseY,
 * the result is written to mouseX/mouseY and the function returns 1. Otherwise,
 * the function returns 0 and it can be assumed that mouseX/mouseY are still
 * valid.
 */
static int pose_to_pointer(
	const XrPosef *pose,
	const float x0,
	const float x1,
	const float y0,
	const float y1,
	const float depth,
	float *mouseX,
	float *mouseY
) {
	float normalDistance = fabsf(pose->position.z - depth);

	/* Convert quaternion to pitch/yaw, we don't care about roll */
	float poseAngleX = asinf(
		-2.0 * (
			(pose->orientation.x * pose->orientation.z) -
			(pose->orientation.w * pose->orientation.y)
		)
	);
	float poseAngleY = atan2f(
		2.0 * (
			(pose->orientation.y * pose->orientation.z) +
			(pose->orientation.w * pose->orientation.x)
		),
		(
			(pose->orientation.w * pose->orientation.w) -
			(pose->orientation.x * pose->orientation.x) -
			(pose->orientation.y * pose->orientation.y) +
			(pose->orientation.z * pose->orientation.z)
		)
	);

	/* flibit's setup starts pitch at +/- 180, flip it around... */
	if (poseAngleY > 0)
	{
		poseAngleY -= M_PI;
	}
	else
	{
		poseAngleY += M_PI;
	}

	/* We have side A and angle A of a right triangle, get the length of side B */
	float offX = sqrtf(powf(normalDistance / cosf(poseAngleX), 2) - (normalDistance * normalDistance));
	float offY = sqrtf(powf(normalDistance / cosf(poseAngleY), 2) - (normalDistance * normalDistance));

	/* Reintroduce the +/- attrib, the above values are always positive */
	offX *= (poseAngleX > 0) - (poseAngleX < 0);
	offY *= (poseAngleY > 0) - (poseAngleY < 0);

	/* Add length of side B to pose->position, normalize result */
	float resultX = ((pose->position.x - offX) - x0) / (x1 - x0);
	float resultY = ((pose->position.y + offY) - y0) / (y1 - y0);

	if (resultX < 0 || resultX > 1 || resultY < 0 || resultY > 1)
	{
		return 0;
	}
	if ((resultX != *mouseX) || (resultY != *mouseY))
	{
		*mouseX = resultX;
		*mouseY = resultY;
		return 1;
	}
	return 0;
}

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
	XrSpace baseSpace = 0, aimSpace = 0;

	/* Error handling */

	XrResult res;
	char resString[XR_MAX_RESULT_STRING_SIZE];

	/* Platform setup */

#ifdef __linux__
	struct uinput_setup usetup;
	struct uinput_abs_setup abssetup;
	struct input_event ie;

	int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
	if (fd == -1)
	{
		printf("uinput could not be opened\n");
		return errno;
	}

	ioctl(fd, UI_SET_EVBIT, EV_KEY);
	ioctl(fd, UI_SET_KEYBIT, KEY_Z);
	ioctl(fd, UI_SET_KEYBIT, KEY_X);
	ioctl(fd, UI_SET_KEYBIT, KEY_C);
	ioctl(fd, UI_SET_KEYBIT, BTN_LEFT);

	ioctl(fd, UI_SET_EVBIT, EV_ABS);
	ioctl(fd, UI_SET_ABSBIT, ABS_X);
	ioctl(fd, UI_SET_ABSBIT, ABS_Y);

	ioctl(fd, UI_SET_EVBIT, EV_SYN);

	memset(&usetup, '\0', sizeof(usetup));
	usetup.id.bustype = BUS_USB;
	usetup.id.vendor = 0x0420;
	usetup.id.product = 0x6969;
	strncpy(usetup.name, "Light Gun XR", sizeof(usetup.name));
	ioctl(fd, UI_DEV_SETUP, &usetup);

	abssetup.absinfo.value = 0;
	abssetup.absinfo.minimum = 0;
	abssetup.absinfo.fuzz = 0;
	abssetup.absinfo.flat = 0;
	abssetup.absinfo.resolution = 0;

	abssetup.code = ABS_X;
	abssetup.absinfo.maximum = SCREEN_WIDTH;
	ioctl(fd, UI_ABS_SETUP, &abssetup);
	abssetup.code = ABS_Y;
	abssetup.absinfo.maximum = SCREEN_HEIGHT;
	ioctl(fd, UI_ABS_SETUP, &abssetup);

	ioctl(fd, UI_DEV_CREATE);

	ie.time.tv_sec = 0;
	ie.time.tv_usec = 0;
#endif

	/* Instance creation */

	XrInstanceCreateInfo instanceCreateInfo;
	const char *extensions[] =
	{
		XR_MND_HEADLESS_EXTENSION_NAME,
		XR_KHR_CONVERT_TIMESPEC_TIME_EXTENSION_NAME
	};
	PFN_xrConvertTimespecTimeToTimeKHR pxrConvertTimespecTimeToTimeKHR;

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
	instanceCreateInfo.enabledExtensionCount = 2;
	instanceCreateInfo.enabledExtensionNames = extensions;

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

	#define CHECK_ERROR(name) \
		if (res != XR_SUCCESS) \
		{ \
			xrResultToString(instance, res, resString); \
			printf(#name ": %s\n", resString); \
			goto cleanup; \
		}

	/* Extensions */

	int returnCode = -2;

	res = xrGetInstanceProcAddr(
		instance,
		"xrConvertTimespecTimeToTimeKHR",
		(PFN_xrVoidFunction*) &pxrConvertTimespecTimeToTimeKHR
	);
	CHECK_ERROR(xrGetInstanceProcAddr)

	/* Action set */

	returnCode = -3;

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

	returnCode = -4;

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

	returnCode = -5;

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

	/* Set up position/rotation tracking */

	returnCode = -6;

	XrReferenceSpaceCreateInfo baseSpaceCreateInfo;
	XrActionSpaceCreateInfo spaceCreateInfo;

	baseSpaceCreateInfo.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
	baseSpaceCreateInfo.next = NULL;
	baseSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
	baseSpaceCreateInfo.poseInReferenceSpace.orientation.x = 0;
	baseSpaceCreateInfo.poseInReferenceSpace.orientation.y = 0;
	baseSpaceCreateInfo.poseInReferenceSpace.orientation.z = 0;
	baseSpaceCreateInfo.poseInReferenceSpace.orientation.w = 1;
	baseSpaceCreateInfo.poseInReferenceSpace.position.x = 0;
	baseSpaceCreateInfo.poseInReferenceSpace.position.y = 0;
	baseSpaceCreateInfo.poseInReferenceSpace.position.z = 0;

	res = xrCreateReferenceSpace(session, &baseSpaceCreateInfo, &baseSpace);
	CHECK_ERROR(xrCreateReferenceSpace)


	spaceCreateInfo.type = XR_TYPE_ACTION_SPACE_CREATE_INFO;
	spaceCreateInfo.next = NULL;
	spaceCreateInfo.action = aim;
	spaceCreateInfo.subactionPath = XR_NULL_PATH;
	spaceCreateInfo.poseInActionSpace.orientation.x = 0;
	spaceCreateInfo.poseInActionSpace.orientation.y = 0;
	spaceCreateInfo.poseInActionSpace.orientation.z = 0;
	spaceCreateInfo.poseInActionSpace.orientation.w = 1;
	spaceCreateInfo.poseInActionSpace.position.x = 0;
	spaceCreateInfo.poseInActionSpace.position.y = 0;
	spaceCreateInfo.poseInActionSpace.position.z = 0;

	res = xrCreateActionSpace(session, &spaceCreateInfo, &aimSpace);
	CHECK_ERROR(xrCreateActionSpace)

	/* Wait for the signal to begin the session */

	returnCode = -7;

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

	returnCode = -8;

	enum
	{
		RECORDING_TOPLEFT,
		RECORDING_BOTTOMRIGHT,
		PLAYING
	} state = RECORDING_TOPLEFT;
	float x0, x1, y0, y1, z;

	int run = 1, sync = 0;
	float mouseX = 0, mouseY = 0;
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

	printf("Light Gun XR has started!\n");
	while (run)
	{
		res = xrSyncActions(session, &syncInfo);
		if (res == XR_SUCCESS)
		{
			struct timespec clock;
			XrTime time;
			XrSpaceLocation aimState;
			XrActionStateBoolean fireState, pedalState, pauseState;

			clock_gettime(CLOCK_MONOTONIC, &clock);
			res = pxrConvertTimespecTimeToTimeKHR(
				instance,
				&clock,
				&time
			);
			CHECK_ERROR(xrConvertTimespecTimeToTimeKHR)

			aimState.type = XR_TYPE_SPACE_LOCATION;
			aimState.next = NULL;
			res = xrLocateSpace(aimSpace, baseSpace, time, &aimState);
			CHECK_ERROR(xrLocateSpace)

			getInfo.action = fire;
			res = xrGetActionStateBoolean(session, &getInfo, &fireState);
			CHECK_ERROR(xrGetActionStateBoolean)

			getInfo.action = pedal;
			res = xrGetActionStateBoolean(session, &getInfo, &pedalState);
			CHECK_ERROR(xrGetActionStateBoolean)

			getInfo.action = pause;
			res = xrGetActionStateBoolean(session, &getInfo, &pauseState);
			CHECK_ERROR(xrGetActionStateBoolean)

			if (state == RECORDING_TOPLEFT)
			{
				if (fireState.currentState && fireState.changedSinceLastSync)
				{
					x0 = aimState.pose.position.x;
					y0 = aimState.pose.position.y;
					z = aimState.pose.position.z;
					state = RECORDING_BOTTOMRIGHT;
					printf("Top left is (%.9f, %.9f, %.9f)\n", x0, y0, z);
				}
			}
			else if (state == RECORDING_BOTTOMRIGHT)
			{
				if (fireState.currentState && fireState.changedSinceLastSync)
				{
					x1 = aimState.pose.position.x;
					y1 = aimState.pose.position.y;
					z = (z < aimState.pose.position.z) ? z : aimState.pose.position.z;
					state = PLAYING;
					printf("Bottom right is (%.9f, %.9f, %.9f)\n", x1, y1, z);
				}
			}
			else
			{
				/* Quit */
				if (fireState.currentState && pauseState.currentState)
				{
					run = 0;
				}

				/* Buttons */
				if (fireState.changedSinceLastSync)
				{
					printf("Fire %s\n", fireState.currentState ? "Press" : "Release");
#ifdef __linux__
					ie.type = EV_KEY;
					ie.code = BTN_LEFT;
					ie.value = fireState.currentState;
					write(fd, &ie, sizeof(ie));
#endif
					sync = 1;
				}
				if (pedalState.changedSinceLastSync)
				{
					printf("Pedal %s\n", pedalState.currentState ? "Press" : "Release");
#ifdef __linux__
					ie.type = EV_KEY;
					ie.code = KEY_Z;
					ie.value = pedalState.currentState;
					write(fd, &ie, sizeof(ie));
#endif
					sync = 1;
				}
				if (pauseState.changedSinceLastSync)
				{
					printf("Pause %s\n", pauseState.currentState ? "Press" : "Release");
#ifdef __linux__
					ie.type = EV_KEY;
					ie.code = KEY_C;
					ie.value = pauseState.currentState;
					write(fd, &ie, sizeof(ie));
#endif
					sync = 1;
				}

				/* Pointer */
				if (pose_to_pointer(
					&aimState.pose,
					x0,
					x1,
					y0,
					y1,
					z,
					&mouseX,
					&mouseY
				)) {
					printf(
						"Pointer: %.9f, %.9f\n",
						mouseX * SCREEN_WIDTH,
						mouseY * SCREEN_HEIGHT
					);
#ifdef __linux__
					ie.type = EV_ABS;
					ie.code = ABS_X;
					ie.value = (int) (mouseX * SCREEN_WIDTH);
					write(fd, &ie, sizeof(ie));
					ie.type = EV_ABS;
					ie.code = ABS_Y;
					ie.value = (int) (mouseY * SCREEN_HEIGHT);
					write(fd, &ie, sizeof(ie));
#endif
					sync = 1;
				}

				if (sync)
				{
#ifdef __linux__
					ie.type = EV_SYN;
					ie.code = SYN_REPORT;
					ie.value = 0;
					write(fd, &ie, sizeof(ie));
#endif
					sync = 0;
				}

				/* TODO: Haptic output */
			}
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
#ifdef __linux__
	ioctl(fd, UI_DEV_DESTROY);
	close(fd);
#endif
	xrDestroySpace(aimSpace);
	xrDestroySpace(baseSpace);
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
