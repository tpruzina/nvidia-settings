/*
 * nvidia-settings: A tool for configuring the NVIDIA X driver on Unix
 * and Linux systems.
 *
 * Copyright (C) 2014 NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses>.
 */

/*
 *  NVML backend
 */

#include <stdlib.h> /* 64 bit malloc */
#include <string.h>
#include <assert.h>

#include "NvCtrlAttributes.h"
#include "NvCtrlAttributesPrivate.h"

#include "msg.h"
#include "parse.h"

#include "NVCtrlLib.h"

#ifdef NVML_AVAILABLE

#include <nvml.h>


#define MAX_NVML_STR_LEN 64


static Bool __isNvmlLoaded = FALSE;
static unsigned int __nvmlUsers = 0;


static void printNvmlError(nvmlReturn_t error)
{
    switch (error) {
        case NVML_SUCCESS:
            break;

        case NVML_ERROR_UNINITIALIZED:
            nv_error_msg("NVML was not first initialized with nvmlInit()");
            break;

        case NVML_ERROR_INVALID_ARGUMENT:
            nv_error_msg("A supplied argument is invalid");
            break;

        case NVML_ERROR_NOT_SUPPORTED:
            nv_error_msg("The requested operation is not available on target "
                         "device");
            break;

        case NVML_ERROR_NO_PERMISSION:
            nv_error_msg("The current user does not have permission for "
                         "operation");
            break;

        case NVML_ERROR_ALREADY_INITIALIZED:
            nv_error_msg("Deprecated: Multiple initializations are now allowed "
                         "through ref counting");
            break;

        case NVML_ERROR_NOT_FOUND:
            nv_error_msg("A query to find an object was unsuccessful");
            break;

        case NVML_ERROR_INSUFFICIENT_SIZE:
            nv_error_msg("An input argument is not large enough");
            break;

        case NVML_ERROR_INSUFFICIENT_POWER:
            nv_error_msg("A device's external power cables are not properly "
                         "attached");
            break;

        case NVML_ERROR_DRIVER_NOT_LOADED:
            nv_error_msg("NVIDIA driver is not loaded");
            break;

        case NVML_ERROR_TIMEOUT:
            nv_error_msg("User provided timeout passed");
            break;

        case NVML_ERROR_IRQ_ISSUE:
            nv_error_msg("NVIDIA Kernel detected an interrupt issue with a "
                         "GPU");
            break;

        case NVML_ERROR_LIBRARY_NOT_FOUND:
            nv_error_msg("NVML Shared Library couldn't be found or loaded");
            break;

        case NVML_ERROR_FUNCTION_NOT_FOUND:
            nv_error_msg("Local version of NVML doesn't implement this "
                         "function");
            break;

        case NVML_ERROR_CORRUPTED_INFOROM:
            nv_error_msg("infoROM is corrupted");
            break;

        case NVML_ERROR_GPU_IS_LOST:
            nv_error_msg("The GPU has fallen off the bus or has otherwise "
                         "become inaccessible");
            break;

        case NVML_ERROR_RESET_REQUIRED:
            nv_error_msg("The GPU requires a reset before it can be used "
                         "again");
            break;

        case NVML_ERROR_OPERATING_SYSTEM:
            nv_error_msg("The GPU control device has been blocked by the "
                         "operating system/cgroups");
            break;

        case NVML_ERROR_UNKNOWN:
            nv_error_msg("An internal driver error occurred");
            break;
    }
}

#endif // NVML_AVAILABLE


/*
 * Loads and initializes the NVML library
 */

ReturnStatus NvCtrlInitNvml(void)
{
#ifdef NVML_AVAILABLE

    if (!__isNvmlLoaded) {
        nvmlReturn_t ret = nvmlInit();
        if (ret != NVML_SUCCESS) {
            printNvmlError(ret);
            return NvCtrlMissingExtension;
        }

        __isNvmlLoaded = TRUE;
    }

    __nvmlUsers++;

    return NvCtrlSuccess;

#else
    return NvCtrlMissingExtension;
#endif
}



/*
 * Unloads the NVML library if it was successfully loaded.
 */

ReturnStatus NvCtrlDestroyNvml(void)
{
#ifdef NVML_AVAILABLE

    if (__isNvmlLoaded) {
        __nvmlUsers--;
        if (__nvmlUsers == 0) {
            nvmlReturn_t ret = nvmlShutdown();
            if (ret != NVML_SUCCESS) {
                printNvmlError(ret);
                return NvCtrlError;
            }
            __isNvmlLoaded = FALSE;
        }
    }
    return NvCtrlSuccess;

#else
    return NvCtrlMissingExtension;
#endif
}


/*
 * Creates and fills an IDs dictionary so we can translate from NV-CONTROL IDs
 * to NVML indexes
 *
 * XXX Needed while using NV-CONTROL as fallback during the migration process
 */

#ifdef NVML_AVAILABLE

static void matchNvCtrlWithNvmlIds(const NvCtrlAttributePrivateHandle *h,
                                   int deviceCount,
                                   unsigned int **idsDictionary)
{
    char nvmlUUID[MAX_NVML_STR_LEN];
    char *nvctrlUUID = NULL;
    nvmlDevice_t device;
    int i, j;

    *idsDictionary = nvalloc(deviceCount * sizeof(unsigned int));

    /* Fallback case is to use same id either for NV-CONTROL and NVML */
    for (i = 0; i < deviceCount; i++) {
        (*idsDictionary)[i] = i;
    }
    
    if (h->nv != NULL) {
        for (i = 0; i < deviceCount; i++) {
            /* Get GPU UUID through NV-CONTROL */
            if (!XNVCTRLQueryTargetStringAttribute(h->dpy,
                                                   NV_CTRL_TARGET_TYPE_GPU,
                                                   i, 0,
                                                   NV_CTRL_STRING_GPU_UUID,
                                                   &nvctrlUUID)) {
                continue;
            }

            if (nvctrlUUID == NULL) {
                continue;
            }

            /* Look for the same UUID through NVML */
            for (j = 0; j < deviceCount; j++) {
                if (NVML_SUCCESS != nvmlDeviceGetHandleByIndex(j, &device)) {
                    continue;
                }

                if (NVML_SUCCESS != nvmlDeviceGetUUID(device, nvmlUUID,
                                                      MAX_NVML_STR_LEN)) {
                    continue;
                }

                if (strcmp(nvctrlUUID, nvmlUUID) == 0) {
                    /* We got a match */
                    (*idsDictionary)[i] = j;
                    break;
                }
            }

            XFree(nvctrlUUID);
        }
    }
}

#endif // NVML_AVAILABLE



/*
 * Initializes an NVML private handle to hold some information to be used later
 * on
 */

NvCtrlNvmlAttributes *NvCtrlInitNvmlAttributes(NvCtrlAttributePrivateHandle *h)
{
#ifdef NVML_AVAILABLE

    NvCtrlNvmlAttributes *nvml = NULL;
    unsigned int count;
    unsigned int *nvctrlToNvmlId;
    int i;

    /* Check parameters */
    if (!__isNvmlLoaded || h == NULL ||
        !TARGET_TYPE_IS_NVML_COMPATIBLE(h->target_type)) {
        goto fail;
    }

    /* Create storage for NVML attributes */
    nvml = nvalloc(sizeof(NvCtrlNvmlAttributes));

    /* Initialize NVML attributes */
    if (nvmlDeviceGetCount(&count) != NVML_SUCCESS) {
        goto fail;
    }
    nvml->deviceCount = count;

    nvml->sensorCountPerGPU = calloc(count, sizeof(unsigned int));
    nvml->sensorCount = 0;
    nvml->coolerCountPerGPU = calloc(count, sizeof(unsigned int));
    nvml->coolerCount = 0;
    if ((nvml->sensorCountPerGPU == NULL) ||
        (nvml->coolerCountPerGPU == NULL)) {

        goto fail;
    }

    /* Fill the NV-CONTROL to NVML IDs dictionary */
    matchNvCtrlWithNvmlIds(h, count, &nvctrlToNvmlId);

    /*
     * Fill 'sensorCountPerGPU', 'coolerCountPerGPU' and properly set
     * 'deviceIdx'
     */
    nvml->deviceIdx = h->target_id; /* Fallback */

    if (h->target_type == GPU_TARGET) {
        nvml->deviceIdx = nvctrlToNvmlId[h->target_id];
    }

    for (i = 0; i < count; i++) {
        int devIdx = nvctrlToNvmlId[i];
        nvmlDevice_t device;
        nvmlReturn_t ret = nvmlDeviceGetHandleByIndex(devIdx, &device);
        if (ret == NVML_SUCCESS) {
            unsigned int temp;
            unsigned int speed;

            /*
             * XXX Currently, NVML only allows to get the GPU temperature so
             *     check for nvmlDeviceGetTemperature() success to figure
             *     out if that sensor is available.
             */
            ret = nvmlDeviceGetTemperature(device, NVML_TEMPERATURE_GPU,
                                           &temp);
            if (ret == NVML_SUCCESS) {
                if ((h->target_type == THERMAL_SENSOR_TARGET) &&
                    (h->target_id == nvml->sensorCount)) {

                    nvml->deviceIdx = devIdx;
                }

                nvml->sensorCountPerGPU[i] = 1;
                nvml->sensorCount++;
            }

            /*
             * XXX NVML assumes at most 1 fan per GPU so check for
             *     nvmlDeviceGetFanSpeed succes to figure out if that fan is
             *     available.
             */
            ret = nvmlDeviceGetFanSpeed(device, &speed);
            if (ret == NVML_SUCCESS) {
                if ((h->target_type == COOLER_TARGET) &&
                    (h->target_id == nvml->coolerCount)) {

                    nvml->deviceIdx = devIdx;
                }

                nvml->coolerCountPerGPU[i] = 1;
                nvml->coolerCount++;
            }
        }
    }

    free(nvctrlToNvmlId);

    return nvml;

 fail:
    free(nvml->sensorCountPerGPU);
    free(nvml->coolerCountPerGPU);
    free(nvml);
    return NULL;

#else
    return NULL;
#endif
}



/*
 * Frees any resource hold by the NVML private handle
 */

void NvCtrlNvmlAttributesClose(NvCtrlAttributePrivateHandle *h)
{
#ifdef NVML_AVAILABLE

    /* Check parameters */
    if (h == NULL || h->nvml == NULL) {
        return;
    }

    free(h->nvml->sensorCountPerGPU);
    free(h->nvml->coolerCountPerGPU);
    free(h->nvml);
    h->nvml = NULL;

#endif
}



/*
 * Get the number of 'target_type' targets according to NVML
 */

ReturnStatus NvCtrlNvmlQueryTargetCount(const CtrlTarget *ctrl_target,
                                        int target_type, int *val)
{
#ifdef NVML_AVAILABLE

    const NvCtrlAttributePrivateHandle *h = getPrivateHandleConst(ctrl_target);

    if (!__isNvmlLoaded) {
        return NvCtrlMissingExtension;
    }

    /*
     * This should't be reached for target types that are not handled through
     * NVML (Keep TARGET_TYPE_IS_NVML_COMPATIBLE in NvCtrlAttributesPrivate.h up
     * to date).
     */
    assert(TARGET_TYPE_IS_NVML_COMPATIBLE(target_type));

    if ((h == NULL) || (h->nvml == NULL)) {
        return NvCtrlBadHandle;
    }

    switch (target_type) {
        case GPU_TARGET:
            *val = (int)(h->nvml->deviceCount);
            break;
        case THERMAL_SENSOR_TARGET:
            *val = (int)(h->nvml->sensorCount);
            break;
        case COOLER_TARGET:
            *val = (int)(h->nvml->coolerCount);
            break;
        default:
            return NvCtrlBadArgument;
    }

    return NvCtrlSuccess;

#else
    return NvCtrlMissingExtension;
#endif
}



/*
 * Get NVML String Attribute Values
 */

#ifdef NVML_AVAILABLE

static ReturnStatus NvCtrlNvmlGetGPUStringAttribute(const CtrlTarget *ctrl_target,
                                                    int attr, char **ptr)
{
    char res[MAX_NVML_STR_LEN];
    const NvCtrlAttributePrivateHandle *h = getPrivateHandleConst(ctrl_target);
    nvmlDevice_t device;
    nvmlReturn_t ret;
    *ptr = NULL;

    if ((h == NULL) || (h->nvml == NULL)) {
        return NvCtrlBadHandle;
    }

    ret = nvmlDeviceGetHandleByIndex(h->nvml->deviceIdx, &device);
    if (ret == NVML_SUCCESS) {
        switch (attr) {
            case NV_CTRL_STRING_PRODUCT_NAME:
                ret = nvmlDeviceGetName(device, res, MAX_NVML_STR_LEN);
                break;

            case NV_CTRL_STRING_VBIOS_VERSION:
                ret = nvmlDeviceGetVbiosVersion(device, res, MAX_NVML_STR_LEN);
                break;

            case NV_CTRL_STRING_GPU_UUID:
                ret = nvmlDeviceGetUUID(device, res, MAX_NVML_STR_LEN);
                break;

            case NV_CTRL_STRING_NVIDIA_DRIVER_VERSION:
            case NV_CTRL_STRING_SLI_MODE:
            case NV_CTRL_STRING_PERFORMANCE_MODES:
            case NV_CTRL_STRING_GPU_CURRENT_CLOCK_FREQS:
            case NV_CTRL_STRING_GPU_UTILIZATION:
            case NV_CTRL_STRING_MULTIGPU_MODE:
            case NV_CTRL_STRING_GVIO_FIRMWARE_VERSION: 
                /*
                 * XXX We'll eventually need to add support for this attributes
                 *     through NVML
                 */
                return NvCtrlNotSupported;

            default:
                /* Did we forget to handle a GPU string attribute? */
                nv_warning_msg("Unhandled string attribute %s (%d) of GPU (%d)",
                               STR_ATTRIBUTE_NAME(attr), attr,
                               NvCtrlGetTargetId(ctrl_target));
                return NvCtrlNotSupported;
        }

        if (ret == NVML_SUCCESS) {
            *ptr = strdup(res);
            return NvCtrlSuccess;
        }
    }

    /* An NVML error occurred */
    printNvmlError(ret);
    return NvCtrlNotSupported;
}

#endif // NVML_AVAILABLE

ReturnStatus NvCtrlNvmlGetStringAttribute(const CtrlTarget *ctrl_target,
                                          int attr, char **ptr)
{
#ifdef NVML_AVAILABLE

    if (!__isNvmlLoaded) {
        return NvCtrlMissingExtension;
    }

    /*
     * This should't be reached for target types that are not handled through
     * NVML (Keep TARGET_TYPE_IS_NVML_COMPATIBLE in NvCtrlAttributesPrivate.h up
     * to date).
     */
    assert(TARGET_TYPE_IS_NVML_COMPATIBLE(NvCtrlGetTargetType(ctrl_target)));

    switch (NvCtrlGetTargetType(ctrl_target)) {
        case GPU_TARGET:
            return NvCtrlNvmlGetGPUStringAttribute(ctrl_target, attr, ptr);

        case THERMAL_SENSOR_TARGET:
            /* Did we forget to handle a sensor string attribute? */
            nv_warning_msg("Unhandled string attribute %s (%d) of Thermal "
                           "sensor (%d)", STR_ATTRIBUTE_NAME(attr), attr,
                           NvCtrlGetTargetId(ctrl_target));
            return NvCtrlNotSupported;

        case COOLER_TARGET:
            /* Did we forget to handle a cooler string attribute? */
            nv_warning_msg("Unhandled string attribute %s (%d) of Fan (%d)",
                           STR_ATTRIBUTE_NAME(attr), attr,
                           NvCtrlGetTargetId(ctrl_target));
            return NvCtrlNotSupported;

        default:
            return NvCtrlBadHandle;
    }

#else
    return NvCtrlMissingExtension;
#endif
}



/*
 * Set NVML String Attribute Values
 */

#ifdef NVML_AVAILABLE

static ReturnStatus NvCtrlNvmlSetGPUStringAttribute(CtrlTarget *ctrl_target,
                                                    int attr, const char *ptr)
{
    const NvCtrlAttributePrivateHandle *h = getPrivateHandleConst(ctrl_target);
    nvmlDevice_t device;
    nvmlReturn_t ret;

    if ((h == NULL) || (h->nvml == NULL)) {
        return NvCtrlBadHandle;
    }

    ret = nvmlDeviceGetHandleByIndex(h->nvml->deviceIdx, &device);
    if (ret == NVML_SUCCESS) {
        switch (attr) {
            case NV_CTRL_STRING_GPU_CURRENT_CLOCK_FREQS:
                /*
                 * XXX We'll eventually need to add support for this attributes
                 *     through NVML
                 */
                return NvCtrlNotSupported;

            default:
                /* Did we forget to handle a GPU string attribute? */
                nv_warning_msg("Unhandled string attribute %s (%d) of GPU (%d) "
                               "(set to '%s')", STR_ATTRIBUTE_NAME(attr), attr,
                               NvCtrlGetTargetId(ctrl_target), (ptr ? ptr : ""));
                return NvCtrlNotSupported;
        }

        if (ret == NVML_SUCCESS) {
            return NvCtrlSuccess;
        }
    }

    /* An NVML error occurred */
    printNvmlError(ret);
    return NvCtrlNotSupported;
}

#endif // NVML_AVAILABLE

ReturnStatus NvCtrlNvmlSetStringAttribute(CtrlTarget *ctrl_target,
                                          int attr, const char *ptr)
{
#ifdef NVML_AVAILABLE

    if (!__isNvmlLoaded) {
        return NvCtrlMissingExtension;
    }

    /*
     * This should't be reached for target types that are not handled through
     * NVML (Keep TARGET_TYPE_IS_NVML_COMPATIBLE in NvCtrlAttributesPrivate.h up
     * to date).
     */
    assert(TARGET_TYPE_IS_NVML_COMPATIBLE(NvCtrlGetTargetType(ctrl_target)));

    switch (NvCtrlGetTargetType(ctrl_target)) {
        case GPU_TARGET:
            return NvCtrlNvmlSetGPUStringAttribute(ctrl_target, attr, ptr);

        case THERMAL_SENSOR_TARGET:
            /* Did we forget to handle a sensor string attribute? */
            nv_warning_msg("Unhandled string attribute %s (%d) of Thermal "
                           "sensor (%d) (set to '%s')",
                           STR_ATTRIBUTE_NAME(attr), attr,
                           NvCtrlGetTargetId(ctrl_target), (ptr ? ptr : ""));
            return NvCtrlNotSupported;

        case COOLER_TARGET:
            /* Did we forget to handle a cooler string attribute? */
            nv_warning_msg("Unhandled string attribute %s (%d) of Fan (%d) "
                           "(set to '%s')", STR_ATTRIBUTE_NAME(attr), attr,
                           NvCtrlGetTargetId(ctrl_target), (ptr ? ptr : ""));
            return NvCtrlNotSupported;

        default:
            return NvCtrlBadHandle;
    }

#else
    return NvCtrlMissingExtension;
#endif
}



/*
 * Get NVML Attribute Values
 */

#ifdef NVML_AVAILABLE

static ReturnStatus NvCtrlNvmlGetGPUAttribute(const CtrlTarget *ctrl_target,
                                              int attr, int64_t *val)
{
    unsigned int res;
    const NvCtrlAttributePrivateHandle *h = getPrivateHandleConst(ctrl_target);
    nvmlDevice_t device;
    nvmlReturn_t ret;

    if ((h == NULL) || (h->nvml == NULL)) {
        return NvCtrlBadHandle;
    }

    ret = nvmlDeviceGetHandleByIndex(h->nvml->deviceIdx, &device);
    if (ret == NVML_SUCCESS) {
        switch (attr) {
            case NV_CTRL_TOTAL_DEDICATED_GPU_MEMORY:
            case NV_CTRL_USED_DEDICATED_GPU_MEMORY:
                {
                    nvmlMemory_t memory;
                    ret = nvmlDeviceGetMemoryInfo(device, &memory);
                    if (ret == NVML_SUCCESS) {
                        switch (attr) {
                            case NV_CTRL_TOTAL_DEDICATED_GPU_MEMORY:
                                res = memory.total >> 20; // bytes --> MB
                                break;
                            case NV_CTRL_USED_DEDICATED_GPU_MEMORY:
                                res = memory.used >> 20; // bytes --> MB
                                break;
                        }
                    }
                }
                break;

            case NV_CTRL_PCI_DOMAIN:
            case NV_CTRL_PCI_BUS:
            case NV_CTRL_PCI_DEVICE:
            case NV_CTRL_PCI_FUNCTION:
            case NV_CTRL_PCI_ID:
                {
                    nvmlPciInfo_t pci;
                    ret = nvmlDeviceGetPciInfo(device, &pci);
                    if (ret == NVML_SUCCESS) {
                        switch (attr) {
                            case NV_CTRL_PCI_DOMAIN:
                                res = pci.domain;
                                break;
                            case NV_CTRL_PCI_BUS:
                                res = pci.bus;
                                break;
                            case NV_CTRL_PCI_DEVICE:
                                res = pci.device;
                                break;
                            case NV_CTRL_PCI_FUNCTION:
                                {
                                    char *f = strrchr(pci.busId, '.');
                                    if (f != NULL) {
                                        res = atoi(f + 1);
                                    }
                                    else {
                                        res = 0;
                                    }
                                }
                                break;
                            case NV_CTRL_PCI_ID:
                                res = ((pci.pciDeviceId << 16) & 0xffff0000) |
                                      ((pci.pciDeviceId >> 16) & 0x0000ffff);
                                break;
                        }
                    }
                }
                break;

            case NV_CTRL_GPU_PCIE_GENERATION:
                ret = nvmlDeviceGetMaxPcieLinkGeneration(device, &res);
                break;

            case NV_CTRL_GPU_PCIE_MAX_LINK_WIDTH:
                ret = nvmlDeviceGetMaxPcieLinkWidth(device, &res);
                break;

            case NV_CTRL_VIDEO_RAM:
            case NV_CTRL_GPU_PCIE_CURRENT_LINK_WIDTH:
            case NV_CTRL_GPU_PCIE_MAX_LINK_SPEED:
            case NV_CTRL_GPU_PCIE_CURRENT_LINK_SPEED:
            case NV_CTRL_BUS_TYPE:
            case NV_CTRL_GPU_MEMORY_BUS_WIDTH:
            case NV_CTRL_GPU_CORES:
            case NV_CTRL_IRQ:
            case NV_CTRL_GPU_COOLER_MANUAL_CONTROL:
            case NV_CTRL_GPU_POWER_SOURCE:
            case NV_CTRL_GPU_CURRENT_PERFORMANCE_LEVEL:
            case NV_CTRL_GPU_ADAPTIVE_CLOCK_STATE:
            case NV_CTRL_GPU_POWER_MIZER_MODE:
            case NV_CTRL_GPU_POWER_MIZER_DEFAULT_MODE:
            case NV_CTRL_GPU_DOUBLE_PRECISION_BOOST_IMMEDIATE:
            case NV_CTRL_GPU_DOUBLE_PRECISION_BOOST_REBOOT:
            case NV_CTRL_GPU_ECC_SUPPORTED:
            case NV_CTRL_GPU_ECC_STATUS:
            case NV_CTRL_GPU_ECC_CONFIGURATION:
            case NV_CTRL_GPU_ECC_DEFAULT_CONFIGURATION:
            case NV_CTRL_GPU_ECC_DOUBLE_BIT_ERRORS:
            case NV_CTRL_GPU_ECC_AGGREGATE_DOUBLE_BIT_ERRORS:
            case NV_CTRL_GPU_ECC_CONFIGURATION_SUPPORTED:
            case NV_CTRL_ENABLED_DISPLAYS:
            case NV_CTRL_CONNECTED_DISPLAYS:
            case NV_CTRL_MAX_SCREEN_WIDTH:
            case NV_CTRL_MAX_SCREEN_HEIGHT:
            case NV_CTRL_MAX_DISPLAYS:
            case NV_CTRL_DEPTH_30_ALLOWED:
            case NV_CTRL_MULTIGPU_MASTER_POSSIBLE:
            case NV_CTRL_SLI_MOSAIC_MODE_AVAILABLE:
            case NV_CTRL_BASE_MOSAIC:
            case NV_CTRL_XINERAMA:
            case NV_CTRL_ATTR_NV_MAJOR_VERSION:
            case NV_CTRL_ATTR_NV_MINOR_VERSION:
            case NV_CTRL_OPERATING_SYSTEM:
            case NV_CTRL_NO_SCANOUT:
            case NV_CTRL_GPU_CORE_TEMPERATURE:
            case NV_CTRL_AMBIENT_TEMPERATURE:
            case NV_CTRL_GPU_CURRENT_CLOCK_FREQS:
            case NV_CTRL_GPU_CURRENT_PROCESSOR_CLOCK_FREQS:
            case NV_CTRL_VIDEO_ENCODER_UTILIZATION:
            case NV_CTRL_VIDEO_DECODER_UTILIZATION:
            case NV_CTRL_FRAMELOCK:
            case NV_CTRL_IS_GVO_DISPLAY:
            case NV_CTRL_DITHERING:
            case NV_CTRL_CURRENT_DITHERING:
            case NV_CTRL_DITHERING_MODE:
            case NV_CTRL_CURRENT_DITHERING_MODE:
            case NV_CTRL_DITHERING_DEPTH:
            case NV_CTRL_CURRENT_DITHERING_DEPTH:
            case NV_CTRL_DIGITAL_VIBRANCE:
            case NV_CTRL_IMAGE_SHARPENING_DEFAULT:
            case NV_CTRL_REFRESH_RATE:
            case NV_CTRL_REFRESH_RATE_3:
            case NV_CTRL_COLOR_SPACE:
            case NV_CTRL_COLOR_RANGE:
            case NV_CTRL_SYNCHRONOUS_PALETTE_UPDATES:
            case NV_CTRL_DPY_HDMI_3D:
                /*
                 * XXX We'll eventually need to add support for this attributes
                 *     through NVML
                 */
                return NvCtrlNotSupported;

            default:
                /* Did we forget to handle a GPU integer attribute? */
                nv_warning_msg("Unhandled integer attribute %s (%d) of GPU "
                               "(%d)", INT_ATTRIBUTE_NAME(attr), attr,
                               NvCtrlGetTargetId(ctrl_target));
                return NvCtrlNotSupported;
        }

        if (ret == NVML_SUCCESS) {
            *val = res;
            return NvCtrlSuccess;
        }
    }

    /* An NVML error occurred */
    printNvmlError(ret);
    return NvCtrlNotSupported;
}

static int getThermalCoolerId(const NvCtrlAttributePrivateHandle *h,
                              unsigned int thermalCoolerCount,
                              const unsigned int *thermalCoolerCountPerGPU)
{
    int i, count;

    if ((h->target_id < 0) || (h->target_id >= thermalCoolerCount)) {
        return -1;
    }

    count = 0;
    for (i = 0; i < h->nvml->deviceCount; i++) {
        int tmp = count + thermalCoolerCountPerGPU[i];
        if (h->target_id < tmp) {
            return (h->target_id - count);
        }
        count = tmp;
    }

    return -1;
}

static ReturnStatus NvCtrlNvmlGetThermalAttribute(const CtrlTarget *ctrl_target,
                                                  int attr, int64_t *val)
{
    unsigned int res;
    const NvCtrlAttributePrivateHandle *h = getPrivateHandleConst(ctrl_target);
    int sensorId;
    nvmlDevice_t device;
    nvmlReturn_t ret;

    if ((h == NULL) || (h->nvml == NULL)) {
        return NvCtrlBadHandle;
    }

    /* Get the proper device according to the sensor ID */
    sensorId = getThermalCoolerId(h, h->nvml->sensorCount,
                                  h->nvml->sensorCountPerGPU);
    if (sensorId == -1) {
        return NvCtrlBadHandle;
    }


    ret = nvmlDeviceGetHandleByIndex(h->nvml->deviceIdx, &device);
    if (ret == NVML_SUCCESS) {
        switch (attr) {
            case NV_CTRL_THERMAL_SENSOR_READING:
                ret = nvmlDeviceGetTemperature(device,
                                               NVML_TEMPERATURE_GPU,
                                               &res);
                break;

            case NV_CTRL_THERMAL_SENSOR_PROVIDER:
            case NV_CTRL_THERMAL_SENSOR_TARGET:
                /*
                 * XXX We'll eventually need to add support for this attributes
                 *     through NVML
                 */
                return NvCtrlNotSupported;

            default:
                /* Did we forget to handle a sensor integer attribute? */
                nv_warning_msg("Unhandled integer attribute %s (%d) of "
                               "Thermal sensor (%d)", INT_ATTRIBUTE_NAME(attr),
                               attr, NvCtrlGetTargetId(ctrl_target));
                return NvCtrlNotSupported;
        }

        if (ret == NVML_SUCCESS) {
            *val = res;
            return NvCtrlSuccess;
        }
    }

    /* An NVML error occurred */
    printNvmlError(ret);
    return NvCtrlNotSupported;
}

static ReturnStatus NvCtrlNvmlGetCoolerAttribute(const CtrlTarget *ctrl_target,
                                                 int attr, int64_t *val)
{
    unsigned int res;
    const NvCtrlAttributePrivateHandle *h = getPrivateHandleConst(ctrl_target);
    int coolerId;
    nvmlDevice_t device;
    nvmlReturn_t ret;

    if ((h == NULL) || (h->nvml == NULL)) {
        return NvCtrlBadHandle;
    }

    /* Get the proper device according to the cooler ID */
    coolerId = getThermalCoolerId(h, h->nvml->coolerCount,
                                  h->nvml->coolerCountPerGPU);
    if (coolerId == -1) {
        return NvCtrlBadHandle;
    }


    ret = nvmlDeviceGetHandleByIndex(h->nvml->deviceIdx, &device);
    if (ret == NVML_SUCCESS) {
        switch (attr) {
            case NV_CTRL_THERMAL_COOLER_LEVEL:
                ret = nvmlDeviceGetFanSpeed(device, &res);
                break;

            case NV_CTRL_THERMAL_COOLER_SPEED:
            case NV_CTRL_THERMAL_COOLER_CONTROL_TYPE:
            case NV_CTRL_THERMAL_COOLER_TARGET:
                /*
                 * XXX We'll eventually need to add support for this attributes
                 *     through NVML
                 */
                return NvCtrlNotSupported;

            default:
                /* Did we forget to handle a cooler integer attribute? */
                nv_warning_msg("Unhandled integer attribute %s (%d) of Fan "
                               "(%d)", INT_ATTRIBUTE_NAME(attr), attr,
                               NvCtrlGetTargetId(ctrl_target));
                return NvCtrlNotSupported;
        }

        if (ret == NVML_SUCCESS) {
            *val = res;
            return NvCtrlSuccess;
        }
    }

    /* An NVML error occurred */
    printNvmlError(ret);
    return NvCtrlNotSupported;
}

#endif // NVML_AVAILABLE

ReturnStatus NvCtrlNvmlGetAttribute(const CtrlTarget *ctrl_target,
                                    int attr, int64_t *val)
{
#ifdef NVML_AVAILABLE

    if (!__isNvmlLoaded) {
        return NvCtrlMissingExtension;
    }

    /*
     * This should't be reached for target types that are not handled through
     * NVML (Keep TARGET_TYPE_IS_NVML_COMPATIBLE in NvCtrlAttributesPrivate.h up
     * to date).
     */
    assert(TARGET_TYPE_IS_NVML_COMPATIBLE(NvCtrlGetTargetType(ctrl_target)));

    switch (NvCtrlGetTargetType(ctrl_target)) {
        case GPU_TARGET:
            return NvCtrlNvmlGetGPUAttribute(ctrl_target, attr, val);
        case THERMAL_SENSOR_TARGET:
            return NvCtrlNvmlGetThermalAttribute(ctrl_target, attr, val);
        case COOLER_TARGET:
            return NvCtrlNvmlGetCoolerAttribute(ctrl_target, attr, val);
        default:
            return NvCtrlBadHandle;
    }

#else
    return NvCtrlMissingExtension;
#endif
}



/*
 * Set NVML Attribute Values
 */

#ifdef NVML_AVAILABLE

static ReturnStatus NvCtrlNvmlSetGPUAttribute(CtrlTarget *ctrl_target,
                                              int attr, int index, int val)
{
    const NvCtrlAttributePrivateHandle *h = getPrivateHandleConst(ctrl_target);
    nvmlDevice_t device;
    nvmlReturn_t ret;

    if ((h == NULL) || (h->nvml == NULL)) {
        return NvCtrlBadHandle;
    }

    ret = nvmlDeviceGetHandleByIndex(h->nvml->deviceIdx, &device);
    if (ret == NVML_SUCCESS) {
        switch (attr) {
            case NV_CTRL_GPU_CURRENT_CLOCK_FREQS:
            case NV_CTRL_GPU_POWER_MIZER_MODE:
            case NV_CTRL_GPU_ECC_CONFIGURATION:
            case NV_CTRL_GPU_COOLER_MANUAL_CONTROL:
            case NV_CTRL_DITHERING:
            case NV_CTRL_DITHERING_MODE:
            case NV_CTRL_DITHERING_DEPTH:
            case NV_CTRL_DIGITAL_VIBRANCE:
            case NV_CTRL_COLOR_SPACE:
            case NV_CTRL_COLOR_RANGE:
            case NV_CTRL_SYNCHRONOUS_PALETTE_UPDATES:
                /*
                 * XXX We'll eventually need to add support for this attributes
                 *     through NVML
                 */
                return NvCtrlNotSupported;

            default:
                /* Did we forget to handle a GPU integer attribute? */
                nv_warning_msg("Unhandled integer attribute %s (%d) of GPU "
                               "(%d) (set to %d)", INT_ATTRIBUTE_NAME(attr),
                               attr, NvCtrlGetTargetId(ctrl_target), val);
                return NvCtrlNotSupported;
        }

        if (ret == NVML_SUCCESS) {
            return NvCtrlSuccess;
        }
    }

    /* An NVML error occurred */
    printNvmlError(ret);
    return NvCtrlNotSupported;
}

static ReturnStatus NvCtrlNvmlSetCoolerAttribute(CtrlTarget *ctrl_target,
                                                 int attr, int val)
{
    NvCtrlAttributePrivateHandle *h = getPrivateHandle(ctrl_target);
    int coolerId;
    nvmlDevice_t device;
    nvmlReturn_t ret;

    if ((h == NULL) || (h->nvml == NULL)) {
        return NvCtrlBadHandle;
    }

    /* Get the proper device according to the cooler ID */
    coolerId = getThermalCoolerId(h, h->nvml->coolerCount,
                                  h->nvml->coolerCountPerGPU);
    if (coolerId == -1) {
        return NvCtrlBadHandle;
    }


    ret = nvmlDeviceGetHandleByIndex(h->nvml->deviceIdx, &device);
    if (ret == NVML_SUCCESS) {
        switch (attr) {
            case NV_CTRL_THERMAL_COOLER_LEVEL:
            case NV_CTRL_THERMAL_COOLER_LEVEL_SET_DEFAULT:
                /*
                 * XXX We'll eventually need to add support for this attributes
                 *     through NVML
                 */
                return NvCtrlNotSupported;

            default:
                /* Did we forget to handle a cooler integer attribute? */
                nv_warning_msg("Unhandled integer attribute %s (%d) of Fan "
                               "(%d) (set to %d)", INT_ATTRIBUTE_NAME(attr),
                               attr, NvCtrlGetTargetId(ctrl_target), val);
                return NvCtrlNotSupported;
        }

        if (ret == NVML_SUCCESS) {
            return NvCtrlSuccess;
        }
    }

    /* An NVML error occurred */
    printNvmlError(ret);
    return NvCtrlNotSupported;
}

#endif // NVML_AVAILABLE

ReturnStatus NvCtrlNvmlSetAttribute(CtrlTarget *ctrl_target, int attr,
                                    int index, int val)
{
#ifdef NVML_AVAILABLE

    if (!__isNvmlLoaded) {
        return NvCtrlMissingExtension;
    }

    /*
     * This should't be reached for target types that are not handled through
     * NVML (Keep TARGET_TYPE_IS_NVML_COMPATIBLE in NvCtrlAttributesPrivate.h up
     * to date).
     */
    assert(TARGET_TYPE_IS_NVML_COMPATIBLE(NvCtrlGetTargetType(ctrl_target)));

    switch (NvCtrlGetTargetType(ctrl_target)) {
        case GPU_TARGET:
            return NvCtrlNvmlSetGPUAttribute(ctrl_target, attr, index, val);

        case THERMAL_SENSOR_TARGET:
            /* Did we forget to handle a sensor integer attribute? */
            nv_warning_msg("Unhandled integer attribute %s (%d) of Thermal "
                           "sensor (%d) (set to %d)", INT_ATTRIBUTE_NAME(attr),
                           attr, NvCtrlGetTargetId(ctrl_target), val);
            return NvCtrlNotSupported;

        case COOLER_TARGET:
            return NvCtrlNvmlSetCoolerAttribute(ctrl_target, attr, val);
        default:
            return NvCtrlBadHandle;
    }

#else
    return NvCtrlMissingExtension;
#endif
}



/*
 * Get NVML Binary Attribute Values
 */

#ifdef NVML_AVAILABLE

static ReturnStatus
NvCtrlNvmlGetGPUBinaryAttribute(const CtrlTarget *ctrl_target,
                                int attr, unsigned char **data, int *len)
{
    const NvCtrlAttributePrivateHandle *h = getPrivateHandleConst(ctrl_target);
    nvmlDevice_t device;
    nvmlReturn_t ret;

    if ((h == NULL) || (h->nvml == NULL)) {
        return NvCtrlBadHandle;
    }

    ret = nvmlDeviceGetHandleByIndex(h->nvml->deviceIdx, &device);
    if (ret == NVML_SUCCESS) {
        switch (attr) {
            case NV_CTRL_BINARY_DATA_FRAMELOCKS_USED_BY_GPU:
            case NV_CTRL_BINARY_DATA_VCSCS_USED_BY_GPU:
            case NV_CTRL_BINARY_DATA_COOLERS_USED_BY_GPU:
            case NV_CTRL_BINARY_DATA_THERMAL_SENSORS_USED_BY_GPU:
            case NV_CTRL_BINARY_DATA_DISPLAYS_CONNECTED_TO_GPU:
            case NV_CTRL_BINARY_DATA_DISPLAYS_ON_GPU:
            case NV_CTRL_BINARY_DATA_GPU_FLAGS:
            case NV_CTRL_BINARY_DATA_XSCREENS_USING_GPU:
                /*
                 * XXX We'll eventually need to add support for this attributes
                 *     through NVML
                 */
                return NvCtrlNotSupported;

            default:
                /* Did we forget to handle a GPU binary attribute? */
                nv_warning_msg("Unhandled binary attribute %s (%d) of GPU (%d)",
                               BIN_ATTRIBUTE_NAME(attr), attr,
                               NvCtrlGetTargetId(ctrl_target));
                return NvCtrlNotSupported;
        }

        if (ret == NVML_SUCCESS) {
            return NvCtrlSuccess;
        }
    }

    /* An NVML error occurred */
    printNvmlError(ret);
    return NvCtrlNotSupported;
}

#endif // NVML_AVAILABLE

ReturnStatus
NvCtrlNvmlGetBinaryAttribute(const CtrlTarget *ctrl_target,
                             int attr, unsigned char **data, int *len)
{
#ifdef NVML_AVAILABLE

    if (!__isNvmlLoaded) {
        return NvCtrlMissingExtension;
    }

    /*
     * This should't be reached for target types that are not handled through
     * NVML (Keep TARGET_TYPE_IS_NVML_COMPATIBLE in NvCtrlAttributesPrivate.h up
     * to date).
     */
    assert(TARGET_TYPE_IS_NVML_COMPATIBLE(NvCtrlGetTargetType(ctrl_target)));

    switch (NvCtrlGetTargetType(ctrl_target)) {
        case GPU_TARGET:
            return NvCtrlNvmlGetGPUBinaryAttribute(ctrl_target,
                                                   attr,
                                                   data,
                                                   len);

        case THERMAL_SENSOR_TARGET:
            /* Did we forget to handle a sensor binary attribute? */
            nv_warning_msg("Unhandled binary attribute %s (%d) of Thermal "
                           "sensor (%d)", BIN_ATTRIBUTE_NAME(attr), attr,
                           NvCtrlGetTargetId(ctrl_target));
            return NvCtrlNotSupported;

        case COOLER_TARGET:
            /* Did we forget to handle a cooler binary attribute? */
            nv_warning_msg("Unhandled binary attribute %s (%d) of Fan (%d)",
                           BIN_ATTRIBUTE_NAME(attr), attr,
                           NvCtrlGetTargetId(ctrl_target));
            return NvCtrlNotSupported;

        default:
            return NvCtrlBadHandle;
    }

#else
    return NvCtrlMissingExtension;
#endif
}



/*
 * Get NVML Valid String Attribute Values
 */

#ifdef NVML_AVAILABLE

static ReturnStatus
NvCtrlNvmlGetGPUValidStringAttributeValues(int attr,
                                           CtrlAttributeValidValues *val)
{
    switch (attr) {
        case NV_CTRL_STRING_PRODUCT_NAME:
        case NV_CTRL_STRING_VBIOS_VERSION:
        case NV_CTRL_STRING_NVIDIA_DRIVER_VERSION:
        case NV_CTRL_STRING_SLI_MODE:
        case NV_CTRL_STRING_PERFORMANCE_MODES:
        case NV_CTRL_STRING_MULTIGPU_MODE:
        case NV_CTRL_STRING_GPU_CURRENT_CLOCK_FREQS:
        case NV_CTRL_STRING_GVIO_FIRMWARE_VERSION: 
        case NV_CTRL_STRING_GPU_UUID:
        case NV_CTRL_STRING_GPU_UTILIZATION:
            /*
             * XXX We'll eventually need to add support for this attributes. For
             *     string attributes, NV-CONTROL only sets the attribute type
             *     and permissions so no actual NVML call will be needed.
             */
            return NvCtrlNotSupported;

        default:
            /* The attribute queried is not GPU-targeted */
            return NvCtrlAttributeNotAvailable;
    }

    return NvCtrlSuccess;
}

#endif // NVML_AVAILABLE

ReturnStatus
NvCtrlNvmlGetValidStringAttributeValues(const CtrlTarget *ctrl_target,
                                        int attr,
                                        CtrlAttributeValidValues *val)
{
#ifdef NVML_AVAILABLE
    ReturnStatus ret;

    if (!__isNvmlLoaded) {
        return NvCtrlMissingExtension;
    }

    /*
     * This should't be reached for target types that are not handled through
     * NVML (Keep TARGET_TYPE_IS_NVML_COMPATIBLE in NvCtrlAttributesPrivate.h up
     * to date).
     */
    assert(TARGET_TYPE_IS_NVML_COMPATIBLE(NvCtrlGetTargetType(ctrl_target)));

    switch (NvCtrlGetTargetType(ctrl_target)) {
        case GPU_TARGET:
            ret = NvCtrlNvmlGetGPUValidStringAttributeValues(attr, val);
            break;

        case THERMAL_SENSOR_TARGET:
        case COOLER_TARGET:
            /* The attribute queried is not sensor nor fan-targeted */
            ret = NvCtrlAttributeNotAvailable;
            break;

        default:
            ret = NvCtrlBadHandle;
            break;
    }

    
    /*
     * XXX Did we forgot to handle this attribute? - REMOVE THIS after
     *     nvidia-settings NVML migration work is done
     */
    if (ret == NvCtrlAttributeNotAvailable) {
        const NvCtrlAttributePrivateHandle *h = getPrivateHandleConst(ctrl_target);
        ReturnStatus ret2;

        if (!h->nv) {
            return NvCtrlMissingExtension;
        }

        ret2 = NvCtrlNvControlGetValidStringDisplayAttributeValues(h, 0, attr, val);

        assert(ret2 == NvCtrlAttributeNotAvailable);

        return ret2;
    }

    return ret;

#else
    return NvCtrlMissingExtension;
#endif
}



/*
 * Get NVML Valid Attribute Values
 */

#ifdef NVML_AVAILABLE

static ReturnStatus
NvCtrlNvmlGetGPUValidAttributeValues(const CtrlTarget *ctrl_target, int attr,
                                     CtrlAttributeValidValues *val)
{
    const NvCtrlAttributePrivateHandle *h = getPrivateHandleConst(ctrl_target);
    nvmlDevice_t device;
    nvmlReturn_t ret;

    if ((h == NULL) || (h->nvml == NULL)) {
        return NvCtrlBadHandle;
    }

    ret = nvmlDeviceGetHandleByIndex(h->nvml->deviceIdx, &device);
    if (ret == NVML_SUCCESS) {
        switch (attr) {
            case NV_CTRL_VIDEO_RAM:
            case NV_CTRL_TOTAL_DEDICATED_GPU_MEMORY:
            case NV_CTRL_USED_DEDICATED_GPU_MEMORY:
            case NV_CTRL_PCI_DOMAIN:
            case NV_CTRL_PCI_BUS:
            case NV_CTRL_PCI_DEVICE:
            case NV_CTRL_PCI_FUNCTION:
            case NV_CTRL_PCI_ID:
            case NV_CTRL_GPU_PCIE_GENERATION:
            case NV_CTRL_GPU_PCIE_MAX_LINK_WIDTH:
            case NV_CTRL_GPU_PCIE_CURRENT_LINK_WIDTH:
            case NV_CTRL_GPU_PCIE_MAX_LINK_SPEED:
            case NV_CTRL_GPU_PCIE_CURRENT_LINK_SPEED:
            case NV_CTRL_BUS_TYPE:
            case NV_CTRL_GPU_MEMORY_BUS_WIDTH:
            case NV_CTRL_GPU_CORES:
            case NV_CTRL_IRQ:
            case NV_CTRL_GPU_COOLER_MANUAL_CONTROL:
            case NV_CTRL_GPU_POWER_SOURCE:
            case NV_CTRL_GPU_CURRENT_PERFORMANCE_LEVEL:
            case NV_CTRL_GPU_ADAPTIVE_CLOCK_STATE:
            case NV_CTRL_GPU_POWER_MIZER_MODE:
            case NV_CTRL_GPU_POWER_MIZER_DEFAULT_MODE:
            case NV_CTRL_GPU_DOUBLE_PRECISION_BOOST_IMMEDIATE:
            case NV_CTRL_GPU_DOUBLE_PRECISION_BOOST_REBOOT:
            case NV_CTRL_GPU_ECC_SUPPORTED:
            case NV_CTRL_GPU_ECC_STATUS:
            case NV_CTRL_GPU_ECC_CONFIGURATION:
            case NV_CTRL_GPU_ECC_DEFAULT_CONFIGURATION:
            case NV_CTRL_GPU_ECC_DOUBLE_BIT_ERRORS:
            case NV_CTRL_GPU_ECC_AGGREGATE_DOUBLE_BIT_ERRORS:
            case NV_CTRL_GPU_ECC_CONFIGURATION_SUPPORTED:
            case NV_CTRL_ENABLED_DISPLAYS:
            case NV_CTRL_CONNECTED_DISPLAYS:
            case NV_CTRL_MAX_SCREEN_WIDTH:
            case NV_CTRL_MAX_SCREEN_HEIGHT:
            case NV_CTRL_MAX_DISPLAYS:
            case NV_CTRL_DEPTH_30_ALLOWED:
            case NV_CTRL_MULTIGPU_MASTER_POSSIBLE:
            case NV_CTRL_SLI_MOSAIC_MODE_AVAILABLE:
            case NV_CTRL_BASE_MOSAIC:
            case NV_CTRL_XINERAMA:
            case NV_CTRL_ATTR_NV_MAJOR_VERSION:
            case NV_CTRL_ATTR_NV_MINOR_VERSION:
            case NV_CTRL_OPERATING_SYSTEM:
            case NV_CTRL_NO_SCANOUT:
            case NV_CTRL_GPU_CORE_TEMPERATURE:
            case NV_CTRL_AMBIENT_TEMPERATURE:
            case NV_CTRL_GPU_CURRENT_CLOCK_FREQS:
            case NV_CTRL_GPU_CURRENT_PROCESSOR_CLOCK_FREQS:
            case NV_CTRL_VIDEO_ENCODER_UTILIZATION:
            case NV_CTRL_VIDEO_DECODER_UTILIZATION:
            case NV_CTRL_FRAMELOCK:
            case NV_CTRL_IS_GVO_DISPLAY:
            case NV_CTRL_DITHERING:
            case NV_CTRL_CURRENT_DITHERING:
            case NV_CTRL_DITHERING_MODE:
            case NV_CTRL_CURRENT_DITHERING_MODE:
            case NV_CTRL_DITHERING_DEPTH:
            case NV_CTRL_CURRENT_DITHERING_DEPTH:
            case NV_CTRL_DIGITAL_VIBRANCE:
            case NV_CTRL_IMAGE_SHARPENING_DEFAULT:
            case NV_CTRL_REFRESH_RATE:
            case NV_CTRL_REFRESH_RATE_3:
            case NV_CTRL_COLOR_SPACE:
            case NV_CTRL_COLOR_RANGE:
            case NV_CTRL_SYNCHRONOUS_PALETTE_UPDATES:
            case NV_CTRL_DPY_HDMI_3D:
                /*
                 * XXX We'll eventually need to add support for this attributes
                 *     through NVML
                 */
                return NvCtrlNotSupported;

            default:
                /* The attribute queried is not GPU-targeted */
                return NvCtrlAttributeNotAvailable;
        }

        if (ret == NVML_SUCCESS) {
            return NvCtrlSuccess;
        }
    }

    /* An NVML error occurred */
    printNvmlError(ret);
    return NvCtrlNotSupported;
}

static ReturnStatus
NvCtrlNvmlGetThermalValidAttributeValues(const CtrlTarget *ctrl_target,
                                         int attr,
                                         CtrlAttributeValidValues *val)
{
    const NvCtrlAttributePrivateHandle *h = getPrivateHandleConst(ctrl_target);
    int sensorId;
    nvmlDevice_t device;
    nvmlReturn_t ret;

    if ((h == NULL) || (h->nvml == NULL)) {
        return NvCtrlBadHandle;
    }

    /* Get the proper device and sensor ID according to the target ID */
    sensorId = getThermalCoolerId(h, h->nvml->sensorCount,
                                  h->nvml->sensorCountPerGPU);
    if (sensorId == -1) {
        return NvCtrlBadHandle;
    }


    ret = nvmlDeviceGetHandleByIndex(h->nvml->deviceIdx, &device);
    if (ret == NVML_SUCCESS) {
        switch (attr) {
            case NV_CTRL_THERMAL_SENSOR_READING:
            case NV_CTRL_THERMAL_SENSOR_PROVIDER:
            case NV_CTRL_THERMAL_SENSOR_TARGET:
                /*
                 * XXX We'll eventually need to add support for this attributes
                 *     through NVML
                 */
                return NvCtrlNotSupported;

            default:
                /* The attribute queried is not sensor-targeted */
                return NvCtrlAttributeNotAvailable;
        }

        if (ret == NVML_SUCCESS) {
            return NvCtrlSuccess;
        }
    }

    /* An NVML error occurred */
    printNvmlError(ret);
    return NvCtrlNotSupported;
}

static ReturnStatus
NvCtrlNvmlGetCoolerValidAttributeValues(const CtrlTarget *ctrl_target,
                                        int attr,
                                        CtrlAttributeValidValues *val)
{
    const NvCtrlAttributePrivateHandle *h = getPrivateHandleConst(ctrl_target);
    int coolerId;
    nvmlDevice_t device;
    nvmlReturn_t ret;

    if ((h == NULL) || (h->nvml == NULL)) {
        return NvCtrlBadHandle;
    }

    /* Get the proper device and cooler ID according to the target ID */
    coolerId = getThermalCoolerId(h, h->nvml->coolerCount,
                                  h->nvml->coolerCountPerGPU);
    if (coolerId == -1) {
        return NvCtrlBadHandle;
    }


    ret = nvmlDeviceGetHandleByIndex(h->nvml->deviceIdx, &device);
    if (ret == NVML_SUCCESS) {
        switch (attr) {
            case NV_CTRL_THERMAL_COOLER_LEVEL:
            case NV_CTRL_THERMAL_COOLER_SPEED:
            case NV_CTRL_THERMAL_COOLER_CONTROL_TYPE:
            case NV_CTRL_THERMAL_COOLER_TARGET:
                /*
                 * XXX We'll eventually need to add support for this attributes
                 *     through NVML
                 */
                return NvCtrlNotSupported;

            default:
                /* The attribute queried is not fan-targeted */
                return NvCtrlAttributeNotAvailable;
        }

        if (ret == NVML_SUCCESS) {
            return NvCtrlSuccess;
        }
    }

    /* An NVML error occurred */
    printNvmlError(ret);
    return NvCtrlNotSupported;
}

#endif // NVML_AVAILABLE

ReturnStatus
NvCtrlNvmlGetValidAttributeValues(const CtrlTarget *ctrl_target,
                                  int attr,
                                  CtrlAttributeValidValues *val)
{
#ifdef NVML_AVAILABLE
    ReturnStatus ret;

    if (!__isNvmlLoaded) {
        return NvCtrlMissingExtension;
    }

    /*
     * This should't be reached for target types that are not handled through
     * NVML (Keep TARGET_TYPE_IS_NVML_COMPATIBLE in NvCtrlAttributesPrivate.h up
     * to date).
     */
    assert(TARGET_TYPE_IS_NVML_COMPATIBLE(NvCtrlGetTargetType(ctrl_target)));

    switch (NvCtrlGetTargetType(ctrl_target)) {
        case GPU_TARGET:
            ret = NvCtrlNvmlGetGPUValidAttributeValues(ctrl_target,
                                                       attr,
                                                       val);
            break;

        case THERMAL_SENSOR_TARGET:
            ret = NvCtrlNvmlGetThermalValidAttributeValues(ctrl_target,
                                                           attr,
                                                           val);
            break;

        case COOLER_TARGET:
            ret = NvCtrlNvmlGetCoolerValidAttributeValues(ctrl_target,
                                                          attr,
                                                          val);
            break;

        default:
            ret = NvCtrlBadHandle;
    }

    
    /*
     * XXX Did we forgot to handle this attribute? - REMOVE THIS after
     *     nvidia-settings NVML migration work is done
     */
    if (ret == NvCtrlAttributeNotAvailable) {
        const NvCtrlAttributePrivateHandle *h = getPrivateHandleConst(ctrl_target);
        ReturnStatus ret2;

        if (!h->nv) {
            return NvCtrlMissingExtension;
        }

        ret2 = NvCtrlNvControlGetValidAttributeValues(h, 0, attr, val);

        assert(ret2 == NvCtrlAttributeNotAvailable);

        return ret2;
    }

    return ret;

#else
    return NvCtrlMissingExtension;
#endif
}

