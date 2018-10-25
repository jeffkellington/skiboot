/* Copyright 2016 IBM Corp.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * 	http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <skiboot.h>
#include <device.h>
#include <fsp.h>
#include <pci.h>
#include <pci-cfg.h>
#include <chip.h>
#include <i2c.h>
#include <timebase.h>
#include <hostservices.h>
#include <npu2.h>

#include "ibm-fsp.h"
#include "lxvpd.h"

/* We don't yet create NPU device nodes on ZZ, but these values are correct */
const struct platform_ocapi zz_ocapi = {
	.i2c_engine          = 1,
	.i2c_port            = 4,
	.i2c_reset_addr      = 0x20,
	.i2c_reset_brick2    = (1 << 1),
	.i2c_reset_brick3    = (1 << 6),
	.i2c_reset_brick4    = 0, /* unused */
	.i2c_reset_brick5    = 0, /* unused */
	.i2c_presence_addr   = 0x20,
	.i2c_presence_brick2 = (1 << 2), /* bottom connector */
	.i2c_presence_brick3 = (1 << 7), /* top connector */
	.i2c_presence_brick4 = 0, /* unused */
	.i2c_presence_brick5 = 0, /* unused */
	.odl_phy_swap        = true,
};

#define NPU_BASE 0x5011000
#define NPU_SIZE 0x2c
#define NPU_INDIRECT0	0x8000000009010c3f /* OB0 - no OB3 on ZZ */

static void create_link(struct dt_node *npu, int group, int index)
{
	struct dt_node *link;
	uint32_t lane_mask;
	char namebuf[32];

	snprintf(namebuf, sizeof(namebuf), "link@%x", index);
	link = dt_new(npu, namebuf);

	dt_add_property_string(link, "compatible", "ibm,npu-link");

	dt_add_property_cells(link, "ibm,npu-link-index", index);

	switch (index) {
	case 2:
		lane_mask = 0xf1e000; /* 0-3, 7-10 */
		break;
	case 3:
		lane_mask = 0x00078f; /* 13-16, 20-23 */
		break;
	default:
		assert(0);
	}

	dt_add_property_u64s(link, "ibm,npu-phy", NPU_INDIRECT0);
	dt_add_property_cells(link, "ibm,npu-lane-mask", lane_mask);
	dt_add_property_cells(link, "ibm,npu-group-id", group);
	dt_add_property_u64s(link, "ibm,link-speed", 20000000000ul);
}

/* FIXME: Get rid of this after we get NPU information properly via HDAT/MRW */
static void zz_fix_npu(void)
{
	struct dt_node *npu;
	struct dt_property *prop;

	/* NPU node already exists, but contains no link */
	prlog(PR_DEBUG, "OCAPI: Adding NPU links\n");
	dt_for_each_compatible(dt_root, npu, "ibm,power9-npu") {
		prop = __dt_find_property(npu, "ibm,npu-links");
		if (!prop) {
			prlog(PR_ERR, "OCAPI: cannot find npu-links property on npu\n");
			return;
		}
		dt_del_property(npu, prop);
		dt_add_property_cells(npu, "ibm,npu-links", 2);
		create_link(npu, 1, 2);
		create_link(npu, 2, 3);
	}
}

static void zz_create_ocapi_i2c_bus(void)
{
	struct dt_node *xscom, *i2cm, *i2c_bus;

	prlog(PR_DEBUG, "OCAPI: Adding I2C bus device node for OCAPI reset\n");
	dt_for_each_compatible(dt_root, xscom, "ibm,xscom") {
		i2cm = dt_find_by_name(xscom, "i2cm@a1000");
		if (!i2cm) {
			prlog(PR_DEBUG, "OCAPI: Adding master @a1000\n");
			i2cm = dt_new(xscom, "i2cm@a1000");
			dt_add_property_cells(i2cm, "reg", 0xa1000, 0x1000);
			dt_add_property_strings(i2cm, "compatible",
					     "ibm,power8-i2cm", "ibm,power9-i2cm");
			dt_add_property_cells(i2cm, "#size-cells", 0x0);
			dt_add_property_cells(i2cm, "#address-cells", 0x1);
			dt_add_property_cells(i2cm, "chip-engine#", 0x1);
			dt_add_property_cells(i2cm, "clock-frequency", 0x7735940);
		}

		if (dt_find_by_name(i2cm, "i2c-bus@4"))
			continue;

		prlog(PR_DEBUG, "OCAPI: Adding bus 4\n");
		i2c_bus = dt_new_addr(i2cm, "i2c-bus", 4);
		dt_add_property_cells(i2c_bus, "reg", 4);
		dt_add_property_cells(i2c_bus, "bus-frequency", 0x61a80);
		dt_add_property_strings(i2c_bus, "compatible",
					"ibm,opal-i2c", "ibm,power8-i2c-port",
					"ibm,power9-i2c-port");
	}
}

static void hack_opencapi_setup(void)
{
	zz_fix_npu();
	zz_create_ocapi_i2c_bus();
}

static bool zz_probe(void)
{
	/* FIXME: make this neater when the dust settles */
	if (dt_node_is_compatible(dt_root, "ibm,zz-1s2u") ||
		dt_node_is_compatible(dt_root, "ibm,zz-1s4u") ||
		dt_node_is_compatible(dt_root, "ibm,zz-2s2u") ||
		dt_node_is_compatible(dt_root, "ibm,zz-2s4u")) {

		hack_opencapi_setup();
		return true;
	}

	return false;
}

static uint32_t ibm_fsp_occ_timeout(void)
{
	/* Use a fixed 60s value for now */
	return 60;
}

static void zz_init(void)
{
	hservices_init();
	ibm_fsp_init();
}

DECLARE_PLATFORM(zz) = {
	.name			= "ZZ",
	.probe			= zz_probe,
	.init			= zz_init,
	.exit			= ibm_fsp_exit,
	.cec_power_down		= ibm_fsp_cec_power_down,
	.cec_reboot		= ibm_fsp_cec_reboot,
	/* FIXME: correct once PCI slot into is available */
	.pci_setup_phb		= NULL,
	.pci_get_slot_info	= NULL,
	.pci_probe_complete	= NULL,
	.nvram_info		= fsp_nvram_info,
	.nvram_start_read	= fsp_nvram_start_read,
	.nvram_write		= fsp_nvram_write,
	.occ_timeout		= ibm_fsp_occ_timeout,
	.elog_commit		= elog_fsp_commit,
	.start_preload_resource	= fsp_start_preload_resource,
	.resource_loaded	= fsp_resource_loaded,
	.sensor_read		= ibm_fsp_sensor_read,
	.terminate		= ibm_fsp_terminate,
	.ocapi			= &zz_ocapi,
	.npu2_device_detect	= npu2_i2c_presence_detect,
};
