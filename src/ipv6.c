/*
 * Handle IPv6 settings for network devices
 *
 * Copyright (C) 2009-2012 Olaf Kirch <okir@suse.de>
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/time.h>

#include <wicked/netinfo.h>
#include <wicked/logging.h>
#include <wicked/time.h>
#include <wicked/ipv6.h>
#include <errno.h>

#include "ipv6_priv.h"
#include "util_priv.h"
#include "sysfs.h"

#define NI_PROC_SYS_NET_IPV6_DIR	"/proc/sys/net/ipv6"

/*
 * index values for the variables in ipv6_devconf
 * defined in linux/ipv6.h + NI_IPV6_; we can't
 * include them (redefine of in6_pktinfo, ...).
 *
 * Note, that ipv6 flags start at 0, ipv4 at 1!
 */
enum {
	NI_IPV6_DEVCONF_FORWARDING = 0,
	NI_IPV6_DEVCONF_HOPLIMIT,
	NI_IPV6_DEVCONF_MTU6,
	NI_IPV6_DEVCONF_ACCEPT_RA,
	NI_IPV6_DEVCONF_ACCEPT_REDIRECTS,
	NI_IPV6_DEVCONF_AUTOCONF,
	NI_IPV6_DEVCONF_DAD_TRANSMITS,
	NI_IPV6_DEVCONF_RTR_SOLICITS,
	NI_IPV6_DEVCONF_RTR_SOLICIT_INTERVAL,
	NI_IPV6_DEVCONF_RTR_SOLICIT_DELAY,
	NI_IPV6_DEVCONF_USE_TEMPADDR,
	NI_IPV6_DEVCONF_TEMP_VALID_LFT,
	NI_IPV6_DEVCONF_TEMP_PREFERED_LFT,
	NI_IPV6_DEVCONF_REGEN_MAX_RETRY,
	NI_IPV6_DEVCONF_MAX_DESYNC_FACTOR,
	NI_IPV6_DEVCONF_MAX_ADDRESSES,
	NI_IPV6_DEVCONF_FORCE_MLD_VERSION,
	NI_IPV6_DEVCONF_ACCEPT_RA_DEFRTR,
	NI_IPV6_DEVCONF_ACCEPT_RA_PINFO,
	NI_IPV6_DEVCONF_ACCEPT_RA_RTR_PREF,
	NI_IPV6_DEVCONF_RTR_PROBE_INTERVAL,
	NI_IPV6_DEVCONF_ACCEPT_RA_RT_INFO_MAX_PLEN,
	NI_IPV6_DEVCONF_PROXY_NDP,
	NI_IPV6_DEVCONF_OPTIMISTIC_DAD,
	NI_IPV6_DEVCONF_ACCEPT_SOURCE_ROUTE,
	NI_IPV6_DEVCONF_MC_FORWARDING,
	NI_IPV6_DEVCONF_DISABLE_IPV6,
	NI_IPV6_DEVCONF_ACCEPT_DAD,
	NI_IPV6_DEVCONF_FORCE_TLLAO,
	NI_IPV6_DEVCONF_NDISC_NOTIFY,
	NI_IPV6_DEVCONF_MLDV1_UNSOLICITED_REPORT_INTERVAL,
	NI_IPV6_DEVCONF_MLDV2_UNSOLICITED_REPORT_INTERVAL,
	NI_IPV6_DEVCONF_SUPPRESS_FRAG_NDISC,
	NI_IPV6_DEVCONF_ACCEPT_RA_FROM_LOCAL,
	NI_IPV6_DEVCONF_USE_OPTIMISTIC,
	NI_IPV6_DEVCONF_ACCEPT_RA_MTU,
	NI_IPV6_DEVCONF_STABLE_SECRET,
	NI_IPV6_DEVCONF_USE_OIF_ADDRS_ONLY,
	NI_IPV6_DEVCONF_ACCEPT_RA_MIN_HOP_LIMIT,
	NI_IPV6_DEVCONF_IGNORE_ROUTES_WITH_LINKDOWN,
	NI_IPV6_DEVCONF_DROP_UNICAST_IN_L2_MULTICAST,
	NI_IPV6_DEVCONF_DROP_UNSOLICITED_NA,
	NI_IPV6_DEVCONF_KEEP_ADDR_ON_DOWN,
	NI_IPV6_DEVCONF_RTR_SOLICIT_MAX_INTERVAL,
	NI_IPV6_DEVCONF_SEG6_ENABLED,
	NI_IPV6_DEVCONF_SEG6_REQUIRE_HMAC,
	NI_IPV6_DEVCONF_ENHANCED_DAD,
	NI_IPV6_DEVCONF_ADDR_GEN_MODE,
	NI_IPV6_DEVCONF_DISABLE_POLICY,
	NI_IPV6_DEVCONF_ACCEPT_RA_RT_INFO_MIN_PLEN,
	NI_IPV6_DEVCONF_NDISC_TCLASS,

	NI_IPV6_DEVCONF_MAX
};

/*
 * Map of net/ipv6/conf/<ifname>/<flag name> to constants
 */
static const ni_intmap_t	__ipv6_devconf_sysctl_name_map[] = {
	{ "forwarding",				NI_IPV6_DEVCONF_FORWARDING		},
	{ "hop_limit",				NI_IPV6_DEVCONF_HOPLIMIT		},
	{ "mtu",				NI_IPV6_DEVCONF_MTU6			},
	{ "accept_ra",				NI_IPV6_DEVCONF_ACCEPT_RA		},
	{ "accept_redirects",			NI_IPV6_DEVCONF_ACCEPT_REDIRECTS	},
	{ "autoconf",				NI_IPV6_DEVCONF_AUTOCONF		},
	{ "dad_transmits",			NI_IPV6_DEVCONF_DAD_TRANSMITS		},
	{ "router_solicitations",		NI_IPV6_DEVCONF_RTR_SOLICITS		},
	{ "router_solicitation_interval",	NI_IPV6_DEVCONF_RTR_SOLICIT_INTERVAL	},
	{ "router_solicitation_delay",		NI_IPV6_DEVCONF_RTR_SOLICIT_DELAY	},
	{ "use_tempaddr",			NI_IPV6_DEVCONF_USE_TEMPADDR		},
	{ "temp_valid_lft",			NI_IPV6_DEVCONF_TEMP_VALID_LFT		},
	{ "temp_prefered_lft",			NI_IPV6_DEVCONF_TEMP_PREFERED_LFT	},
	{ "regen_max_retry",			NI_IPV6_DEVCONF_REGEN_MAX_RETRY		},
	{ "max_desync_factor",			NI_IPV6_DEVCONF_MAX_DESYNC_FACTOR	},
	{ "max_addresses",			NI_IPV6_DEVCONF_MAX_ADDRESSES		},
	{ "force_mld_version",			NI_IPV6_DEVCONF_FORCE_MLD_VERSION	},
	{ "accept_ra_defrtr",			NI_IPV6_DEVCONF_ACCEPT_RA_DEFRTR	},
	{ "accept_ra_pinfo",			NI_IPV6_DEVCONF_ACCEPT_RA_PINFO		},
	{ "accept_ra_rtr_pref",			NI_IPV6_DEVCONF_ACCEPT_RA_RTR_PREF	},
	{ "router_probe_interval",		NI_IPV6_DEVCONF_RTR_PROBE_INTERVAL	},
	{ "accept_ra_rt_info_max_plen",		NI_IPV6_DEVCONF_ACCEPT_RA_RT_INFO_MAX_PLEN},
	{ "proxy_ndp",				NI_IPV6_DEVCONF_PROXY_NDP		},
	{ "optimistic_dad",			NI_IPV6_DEVCONF_OPTIMISTIC_DAD		},
	{ "accept_source_route",		NI_IPV6_DEVCONF_ACCEPT_SOURCE_ROUTE	},
	{ "mc_forwarding",			NI_IPV6_DEVCONF_MC_FORWARDING		},
	{ "disable_ipv6",			NI_IPV6_DEVCONF_DISABLE_IPV6		},
	{ "accept_dad",				NI_IPV6_DEVCONF_ACCEPT_DAD		},
	{ "force_tllao",			NI_IPV6_DEVCONF_FORCE_TLLAO		},
	{ "ndisc_notify",			NI_IPV6_DEVCONF_NDISC_NOTIFY		},
	{ "mldv1_unsolicited_report_interval",	NI_IPV6_DEVCONF_MLDV1_UNSOLICITED_REPORT_INTERVAL},
	{ "mldv2_unsolicited_report_interval",	NI_IPV6_DEVCONF_MLDV2_UNSOLICITED_REPORT_INTERVAL},
	{ "suppress_frag_ndisc",		NI_IPV6_DEVCONF_SUPPRESS_FRAG_NDISC	},
	{ "accept_ra_from_local",		NI_IPV6_DEVCONF_ACCEPT_RA_FROM_LOCAL	},
	{ "use_optimistic",			NI_IPV6_DEVCONF_USE_OPTIMISTIC		},
	{ "accept_ra_mtu",			NI_IPV6_DEVCONF_ACCEPT_RA_MTU		},
	{ "stable_secret",			NI_IPV6_DEVCONF_STABLE_SECRET		},
	{ "use_oif_addrs_only",			NI_IPV6_DEVCONF_USE_OIF_ADDRS_ONLY	},
	{ "accept_ra_min_hop_limit",		NI_IPV6_DEVCONF_ACCEPT_RA_MIN_HOP_LIMIT	},
	{ "ignore_routes_with_linkdown",	NI_IPV6_DEVCONF_IGNORE_ROUTES_WITH_LINKDOWN},
	{ "drop_unicast_in_l2_multicast",	NI_IPV6_DEVCONF_DROP_UNICAST_IN_L2_MULTICAST},
	{ "drop_unsolicited_na",		NI_IPV6_DEVCONF_DROP_UNSOLICITED_NA	},
	{ "keep_addr_on_down",			NI_IPV6_DEVCONF_KEEP_ADDR_ON_DOWN	},
	{ "router_solicitation_max_interval",	NI_IPV6_DEVCONF_RTR_SOLICIT_MAX_INTERVAL},
	{ "seg6_enabled",			NI_IPV6_DEVCONF_SEG6_ENABLED		},
	{ "seg6_require_hmac",			NI_IPV6_DEVCONF_SEG6_REQUIRE_HMAC	},
	{ "enhanced_dad",			NI_IPV6_DEVCONF_ENHANCED_DAD		},
	{ "addr_gen_mode",			NI_IPV6_DEVCONF_ADDR_GEN_MODE		},
	{ "disable_policy",			NI_IPV6_DEVCONF_DISABLE_POLICY		},
	{ "accept_ra_rt_info_min_plen",		NI_IPV6_DEVCONF_ACCEPT_RA_RT_INFO_MIN_PLEN},
	{ "ndisc_tclass",			NI_IPV6_DEVCONF_NDISC_TCLASS		},

	{ NULL,					NI_IPV6_DEVCONF_MAX			}
};

/*
 * Check if ipv6 is supported or disabled
 * via ipv6.disabled=1 kernel command line.
 */
ni_bool_t
ni_ipv6_supported(void)
{
	return ni_isdir(NI_PROC_SYS_NET_IPV6_DIR);
}

/*
 * Reset to ipv6 config defaults
 */
static void
ni_ipv6_devconf_reset(ni_ipv6_devconf_t *conf)
{
	conf->enabled = NI_TRISTATE_DEFAULT;
	conf->forwarding = NI_TRISTATE_DEFAULT;
	conf->autoconf = NI_TRISTATE_DEFAULT;
	conf->privacy = NI_IPV6_PRIVACY_DEFAULT;
	conf->accept_ra = NI_IPV6_ACCEPT_RA_DEFAULT;
	conf->accept_dad = NI_IPV6_ACCEPT_DAD_DEFAULT;
	conf->accept_redirects = NI_TRISTATE_DEFAULT;
	conf->addr_gen_mode = NI_IPV6_ADDR_GEN_MODE_DEFAULT;
	conf->stable_secret = in6addr_any;
}

/*
 * Reset to ipv6 config defaults
 */
static void
ni_ipv6_ra_info_reset(ni_ipv6_ra_info_t *radv)
{
	radv->managed_addr = FALSE;
	radv->other_config = FALSE;

	ni_ipv6_ra_info_flush(radv);
}

/*
 * Set the interface's ipv6 info
 */
ni_ipv6_devinfo_t *
ni_netdev_get_ipv6(ni_netdev_t *dev)
{
	if (dev->ipv6 == NULL)
		dev->ipv6 = ni_ipv6_devinfo_new();
	return dev->ipv6;
}

ni_bool_t
ni_netdev_ipv6_is_ready(const ni_netdev_t *dev)
{
	return dev && ni_ipv6_devinfo_is_ready(dev->ipv6);
}

ni_bool_t
ni_netdev_ipv6_ra_received(const ni_netdev_t *dev)
{
	return dev && ni_ipv6_devinfo_ra_received(dev->ipv6);
}

ni_bool_t
ni_netdev_ipv6_ra_requested(const ni_netdev_t *dev)
{
	return dev && ni_ipv6_devinfo_ra_requested(dev->ipv6);
}

void
ni_netdev_set_ipv6(ni_netdev_t *dev, ni_ipv6_devconf_t *conf)
{
	if (conf != NULL) {
		ni_netdev_get_ipv6(dev);
		dev->ipv6->conf = *conf;
	} else if (dev->ipv6) {
		ni_ipv6_devinfo_free(dev->ipv6);
		dev->ipv6 = NULL;
	}
}

ni_ipv6_devinfo_t *
ni_ipv6_devinfo_new(void)
{
	ni_ipv6_devinfo_t *ipv6;

	ipv6 = xcalloc(1, sizeof(*ipv6));
	ni_ipv6_devconf_reset(&ipv6->conf);
	ni_ipv6_ra_info_reset(&ipv6->radv);
	return ipv6;
}

void
ni_ipv6_devinfo_free(ni_ipv6_devinfo_t *ipv6)
{
	if (ipv6)
		ni_ipv6_ra_info_reset(&ipv6->radv);
	free(ipv6);
}

ni_bool_t
ni_ipv6_devinfo_is_ready(const ni_ipv6_devinfo_t *ipv6)
{
	return ipv6 && (ipv6->flags & NI_BIT(NI_IPV6_READY));
}

ni_bool_t
ni_ipv6_devinfo_ra_received(const ni_ipv6_devinfo_t *ipv6)
{
	return ipv6 && (ipv6->flags & NI_BIT(NI_IPV6_RA_RCVD));
}

ni_bool_t
ni_ipv6_devinfo_ra_requested(const ni_ipv6_devinfo_t *ipv6)
{
	return ipv6 && (ipv6->flags & NI_BIT(NI_IPV6_RS_SENT));
}

/*
 * Discover current IPv6 device settings
 */
int
ni_system_ipv6_devinfo_get(ni_netdev_t *dev, ni_ipv6_devinfo_t *ipv6)
{
	if (ipv6 == NULL)
		ipv6 = ni_netdev_get_ipv6(dev);

	if (!ni_ipv6_supported()) {
		ni_ipv6_devconf_reset(&ipv6->conf);
		ni_ipv6_ra_info_reset(&ipv6->radv);
		ipv6->conf.enabled = NI_TRISTATE_DISABLE;
		return 0;
	}

	/*
	 * dhcpcd does something very odd when shutting down an interface;
	 * in addition to removing all IPv4 addresses, it also removes any
	 * IPv6 addresses. The kernel seems to take this as "disable IPv6
	 * on this interface", and subsequently, /proc/sys/ipv6/conf/<ifname>
	 * is gone.
	 * When we bring the interface back up, everything is fine; but until
	 * then we need to ignore this glitch.
	 */
	if (ni_sysctl_ipv6_ifconfig_is_present(dev->name)) {
		int val;

		if (ni_sysctl_ipv6_ifconfig_get_int(dev->name, "disable_ipv6", &val) >= 0)
			ni_tristate_set(&ipv6->conf.enabled, !val);

		if (ni_sysctl_ipv6_ifconfig_get_int(dev->name, "forwarding", &val) >= 0)
			ni_tristate_set(&ipv6->conf.forwarding, !!val);

		if (ni_sysctl_ipv6_ifconfig_get_int(dev->name, "autoconf", &val) >= 0)
			ni_tristate_set(&ipv6->conf.autoconf, !!val);

		if (ni_sysctl_ipv6_ifconfig_get_int(dev->name, "use_tempaddr", &val) >= 0)
			ipv6->conf.privacy = val < -1 ? -1 : (val > 2 ? 2 : val);

		if (ni_sysctl_ipv6_ifconfig_get_int(dev->name, "accept_ra", &val) >= 0)
			ipv6->conf.accept_ra = val < 0 ? 0 : val > 2 ? 2 : val;

		if (ni_sysctl_ipv6_ifconfig_get_int(dev->name, "accept_dad", &val) >= 0)
			ipv6->conf.accept_dad = val < 0 ? 0 : val > 2 ? 2 : val;

		if (ni_sysctl_ipv6_ifconfig_get_int(dev->name, "accept_redirects", &val) >= 0)
			ni_tristate_set(&ipv6->conf.accept_redirects, !!val);

		if (ni_sysctl_ipv6_ifconfig_get_int(dev->name, "addr_gen_mode", &val) >= 0)
			ipv6->conf.addr_gen_mode = val;

		/* omit reading stable_secret, see ni_system_ipv6_devinfo_set */

	} else {
		ni_warn("%s: cannot get ipv6 device attributes", dev->name);

		/* Reset to defaults */
		ni_ipv6_devconf_reset(&ipv6->conf);
		ni_ipv6_ra_info_reset(&ipv6->radv);
	}

	return 0;
}

/*
 * Update the device's IPv6 settings
 */
static inline int
__change_int(const char *ifname, const char *attr, int value)
{
	if (!ni_tristate_is_set(value))
		return 1;

	if (ni_sysctl_ipv6_ifconfig_set_int(ifname, attr, value) < 0) {
		if (errno == EROFS || errno == ENOENT) {
			ni_info("%s: cannot set ipv6.conf.%s = %d attribute: %m",
					ifname, attr, value);
			return 1;
		} else {
			ni_warn("%s: cannot set ipv6.conf.%s = %d attribute: %m",
					ifname, attr, value);
			return -errno;
		}
	}

	return 0;
}

static ni_bool_t
__tristate_changed(ni_tristate_t cfg, ni_tristate_t sys)
{
	return ni_tristate_is_set(cfg) && cfg != sys;
}

int
ni_system_ipv6_devinfo_set(ni_netdev_t *dev, const ni_ipv6_devconf_t *conf)
{
	struct in6_addr stable_secret = in6addr_any;
	ni_ipv6_devinfo_t *ipv6;
	int ret;

	if (!conf || !(ipv6 = ni_netdev_get_ipv6(dev)))
		return -1;

	if (!ni_ipv6_supported()) {
		ipv6->conf.enabled = NI_TRISTATE_DISABLE;
		if (ni_tristate_is_enabled(conf->enabled)) {
			errno = EAFNOSUPPORT;
			return -1;
		}
		return 0;
	}

	if (__tristate_changed(conf->enabled, ipv6->conf.enabled)) {
		ret = __change_int(dev->name, "disable_ipv6",
				ni_tristate_is_enabled(conf->enabled) ? 0 : 1);
		if (ret < 0)
			return ret;
		if (ret == 0)
			ni_tristate_set(&ipv6->conf.enabled, conf->enabled);
	}

	/* If we're disabling IPv6 on this interface, we're done! */
	if (ni_tristate_is_disabled(conf->enabled)) {
		ni_ipv6_ra_info_reset(&dev->ipv6->radv);
		return 0;
	}

	if (__tristate_changed(conf->forwarding, ipv6->conf.forwarding)) {
		ret = __change_int(dev->name, "forwarding", conf->forwarding);
		if (ret < 0)
			return ret;
		if (ret == 0)
			ipv6->conf.forwarding = conf->forwarding;
	}

	if (__tristate_changed(conf->autoconf, ipv6->conf.autoconf)) {
		ret = __change_int(dev->name, "autoconf", conf->autoconf);
		if (ret < 0)
			return ret;
		if (ret == 0)
			ipv6->conf.autoconf = conf->autoconf;
	}

	if (__tristate_changed(conf->privacy, ipv6->conf.privacy)) {
		/* kernel is using -1 for loopback, ptp, ... */
		int privacy = conf->privacy > 2 ? 2 : conf->privacy;
		ret = __change_int(dev->name, "use_tempaddr", privacy);
		if (ret < 0)
			return ret;
		if (ret == 0)
			ipv6->conf.privacy = privacy;
	}

	if (__tristate_changed(conf->accept_ra, ipv6->conf.accept_ra)) {
		int accept_ra = conf->accept_ra > 2 ? 2 : conf->accept_ra;
		ret = __change_int(dev->name, "accept_ra", accept_ra);
		if (ret < 0)
			return ret;
		if (ret == 0)
			ipv6->conf.accept_ra = accept_ra;
	}

	if (__tristate_changed(conf->accept_dad, ipv6->conf.accept_dad)) {
		int accept_dad = conf->accept_dad > 2 ? 2 : conf->accept_dad;
		ret = __change_int(dev->name, "accept_dad", accept_dad);
		if (ret < 0)
			return ret;
		if (ret == 0)
			ipv6->conf.accept_dad = accept_dad;
	}

	if (__tristate_changed(conf->accept_redirects, ipv6->conf.accept_redirects)) {
		ret = __change_int(dev->name, "accept_redirects", conf->accept_redirects);
		if (ret < 0)
			return ret;
		if (ret == 0)
			ipv6->conf.accept_redirects = conf->accept_redirects;
	}

	if (__tristate_changed(conf->addr_gen_mode, ipv6->conf.addr_gen_mode)) {
		ret = __change_int(dev->name, "addr_gen_mode", conf->addr_gen_mode);
		if (ret < 0)
			return ret;
		if (ret == 0)
			ipv6->conf.addr_gen_mode = conf->addr_gen_mode;
	}

	/* netlink omits stable_secret, but because it usually provides *
	 * other sysctls, our sysfs get function (above) isn't called.  *
	 * try avoid i/o errors due to not readable stable_secret.      */
	if (ipv6->conf.addr_gen_mode == NI_IPV6_ADDR_GEN_MODE_STABLE_PRIVACY)
		ni_sysctl_ipv6_ifconfig_get_ipv6(dev->name, "stable_secret", &stable_secret);
	if (memcmp(&in6addr_any, &conf->stable_secret, sizeof(struct in6_addr)) ||
	    (ipv6->conf.addr_gen_mode == NI_IPV6_ADDR_GEN_MODE_STABLE_PRIVACY &&
	     memcmp(&stable_secret, &conf->stable_secret, sizeof(struct in6_addr)))) {
		ret = ni_sysctl_ipv6_ifconfig_set_ipv6(dev->name, "stable_secret",
				conf->stable_secret);
		if (ret < 0)
			return ret;
	}
	return 0;
}

void
ni_ipv6_ra_info_flush(ni_ipv6_ra_info_t *radv)
{
	ni_ipv6_ra_pinfo_list_destroy(&radv->pinfo);
	ni_ipv6_ra_rdnss_list_destroy(&radv->rdnss);
	ni_ipv6_ra_dnssl_list_destroy(&radv->dnssl);
}

unsigned int
ni_ipv6_ra_info_expire(ni_ipv6_ra_info_t *radv, const struct timeval *current)
{
	unsigned int left, lifetime = NI_LIFETIME_INFINITE;
	struct timeval now;

	if (!current || !timerisset(current)) {
		ni_timer_get_time(&now);
		current = &now;
	}

	if ((left = ni_ipv6_ra_pinfo_list_expire(&radv->pinfo, current)) && left < lifetime)
		lifetime = left;

	if ((left = ni_ipv6_ra_rdnss_list_expire(&radv->rdnss, current)) && left < lifetime)
		lifetime = left;

	if ((left = ni_ipv6_ra_dnssl_list_expire(&radv->dnssl, current)) && left < lifetime)
		lifetime = left;

	return lifetime;
}

void
ni_ipv6_ra_pinfo_free(ni_ipv6_ra_pinfo_t *pi)
{
	free(pi);
}

void
ni_ipv6_ra_pinfo_list_prepend(ni_ipv6_ra_pinfo_t **list, ni_ipv6_ra_pinfo_t *pi)
{
	pi->next = *list;
	*list = pi;
}

void
ni_ipv6_ra_pinfo_list_destroy(ni_ipv6_ra_pinfo_t **list)
{
	ni_ipv6_ra_pinfo_t *pi;

	while ((pi = *list) != NULL) {
		*list = pi->next;
		ni_ipv6_ra_pinfo_free(pi);
	}
}

unsigned int
ni_ipv6_ra_pinfo_list_expire(ni_ipv6_ra_pinfo_t **list, const struct timeval *current)
{
	unsigned int left, lifetime = NI_LIFETIME_INFINITE;
	ni_ipv6_ra_pinfo_t *cur, **pos;

	if (!list)
		return lifetime;

	for (pos = list; (cur = *pos) != NULL; ) {
		left = ni_lifetime_left(cur->valid_lft, &cur->acquired, current);
		if (left) {
			if (left < lifetime)
				lifetime = left;
			pos = &cur->next;
		} else {
			*pos = cur->next;
			ni_ipv6_ra_pinfo_free(cur);
		}
	}
	return lifetime;
}

ni_ipv6_ra_pinfo_t *
ni_ipv6_ra_pinfo_list_remove(ni_ipv6_ra_pinfo_t **list, const ni_ipv6_ra_pinfo_t *pi)
{
	ni_ipv6_ra_pinfo_t **pos, *cur;

	for (pos = list; (cur = *pos) != NULL; pos = &cur->next) {
		if (cur->length != pi->length)
			continue;
		if (ni_sockaddr_equal(&cur->prefix, &pi->prefix)) {
			*pos = cur->next;
			cur->next = NULL;
			return cur;
		}
	}
	return NULL;
}

static ni_ipv6_ra_rdnss_t *
ni_ipv6_ra_rdnss_new()
{
	return calloc(1, sizeof(ni_ipv6_ra_rdnss_t));
}

static void
ni_ipv6_ra_rdnss_free(ni_ipv6_ra_rdnss_t *rdnss)
{
	free(rdnss);
}

void
ni_ipv6_ra_rdnss_list_destroy(ni_ipv6_ra_rdnss_t **list)
{
	ni_ipv6_ra_rdnss_t *rdnss;

	while ((rdnss = *list)) {
		*list = rdnss->next;
		ni_ipv6_ra_rdnss_free(rdnss);
	}
}

unsigned int
ni_ipv6_ra_rdnss_list_expire(ni_ipv6_ra_rdnss_t **list, const struct timeval *current)
{
	unsigned int left, lifetime = NI_LIFETIME_INFINITE;
	ni_ipv6_ra_rdnss_t *cur, **pos;

	if (!list)
		return lifetime;

	for (pos = list; (cur = *pos) != NULL; ) {
		left = ni_lifetime_left(cur->lifetime, &cur->acquired, current);
		if (left) {
			if (left < lifetime)
				lifetime = left;
			pos = &cur->next;
		} else {
			*pos = cur->next;
			ni_ipv6_ra_rdnss_free(cur);
		}
	}
	return lifetime;
}

ni_bool_t
ni_ipv6_ra_rdnss_list_update(ni_ipv6_ra_rdnss_t **list, const struct in6_addr *ipv6,
				unsigned int lifetime, const struct timeval *acquired)
{
	ni_ipv6_ra_rdnss_t *rdnss, **pos;
	ni_sockaddr_t addr;

	if (!list || !ipv6 || !acquired)
		return FALSE;

	ni_sockaddr_set_ipv6(&addr, *ipv6, 0);
	for (pos = list; (rdnss = *pos); pos = &rdnss->next) {
		if (ni_sockaddr_equal(&rdnss->server, &addr)) {
			if (lifetime) {
				rdnss->lifetime = lifetime;
				rdnss->acquired = *acquired;
			} else {
				*pos = rdnss->next;
				ni_ipv6_ra_rdnss_free(rdnss);
			}
			return TRUE;
		}
	}

	if (lifetime)  {
		rdnss = ni_ipv6_ra_rdnss_new();
		if (rdnss) {
			rdnss->server   = addr;
			rdnss->lifetime = lifetime;
			rdnss->acquired = *acquired;
			*pos = rdnss;
			return TRUE;
		}
		return FALSE;
	}

	/* just nothing to do on removal event for untracked server */
	return TRUE;
}

static ni_ipv6_ra_dnssl_t *
ni_ipv6_ra_dnssl_new()
{
	return calloc(1, sizeof(ni_ipv6_ra_dnssl_t));
}

static void
ni_ipv6_ra_dnssl_free(ni_ipv6_ra_dnssl_t *dnssl)
{
	if (dnssl) {
		ni_string_free(&dnssl->domain);
		free(dnssl);
	}
}

void
ni_ipv6_ra_dnssl_list_destroy(ni_ipv6_ra_dnssl_t **list)
{
	ni_ipv6_ra_dnssl_t *dnssl;

	while ((dnssl = *list)) {
		*list = dnssl->next;
		ni_ipv6_ra_dnssl_free(dnssl);
	}
}

unsigned int
ni_ipv6_ra_dnssl_list_expire(ni_ipv6_ra_dnssl_t **list, const struct timeval *current)
{
	unsigned int left, lifetime = NI_LIFETIME_INFINITE;
	ni_ipv6_ra_dnssl_t *cur, **pos;

	if (!list)
		return lifetime;

	for (pos = list; (cur = *pos) != NULL; ) {
		left = ni_lifetime_left(cur->lifetime, &cur->acquired, current);
		if (left) {
			if (left < lifetime)
				lifetime = left;
			pos = &cur->next;
		} else {
			*pos = cur->next;
			ni_ipv6_ra_dnssl_free(cur);
		}
	}
	return lifetime;
}

ni_bool_t
ni_ipv6_ra_dnssl_list_update(ni_ipv6_ra_dnssl_t **list, const char *domain,
				unsigned int lifetime, const struct timeval *acquired)
{
	ni_ipv6_ra_dnssl_t *dnssl, **pos;

	if (!list || ni_string_empty(domain) || !acquired)
		return FALSE;

	for (pos = list; (dnssl = *pos); pos = &dnssl->next) {
		if (ni_string_eq_nocase(dnssl->domain, domain)) {
			if (lifetime) {
				dnssl->lifetime = lifetime;
				dnssl->acquired = *acquired;
			} else {
				*pos = dnssl->next;
				ni_ipv6_ra_dnssl_free(dnssl);
			}
			return TRUE;
		}
	}

	if (lifetime)  {
		dnssl = ni_ipv6_ra_dnssl_new();
		if (dnssl) {
			dnssl->lifetime = lifetime;
			dnssl->acquired = *acquired;
			if (ni_string_dup(&dnssl->domain, domain)) {
				ni_string_tolower(dnssl->domain);
				*pos = dnssl;
				return TRUE;
			}
			ni_ipv6_ra_dnssl_free(dnssl);
		}
		return FALSE;
	}

	/* just nothing to do on removal event for untracked domain */
	return TRUE;
}

const char *
ni_ipv6_devconf_privacy_to_name(int privacy)
{
	static const ni_intmap_t	__privacy_names[] = {
		{ "default",		NI_IPV6_PRIVACY_DEFAULT		},
		{ "disable",		NI_IPV6_PRIVACY_DISABLED	},
		{ "prefer-public",	NI_IPV6_PRIVACY_PREFER_PUBLIC	},
		{ "prefer-temporary",	NI_IPV6_PRIVACY_PREFER_TEMPORARY},
		{ NULL,			NI_IPV6_PRIVACY_DEFAULT		}
	};
	if (privacy < NI_IPV6_PRIVACY_DEFAULT)
		privacy = NI_IPV6_PRIVACY_DEFAULT;
	else
	if (privacy > NI_IPV6_PRIVACY_PREFER_TEMPORARY)
		privacy = NI_IPV6_PRIVACY_PREFER_TEMPORARY;

	return ni_format_uint_mapped(privacy, __privacy_names);
}

const char *
ni_ipv6_devconf_accept_ra_to_name(int accept_ra)
{
	static const ni_intmap_t	__accept_ra_names[] = {
		{ "disable",		NI_IPV6_ACCEPT_RA_DISABLED	},
		{ "host",		NI_IPV6_ACCEPT_RA_HOST		},
		{ "router",		NI_IPV6_ACCEPT_RA_ROUTER	},
		{ NULL,			NI_IPV6_ACCEPT_RA_DEFAULT	}
	};
	if (accept_ra < NI_IPV6_ACCEPT_RA_DEFAULT)
		accept_ra = NI_IPV6_ACCEPT_RA_DEFAULT;
	else
	if (accept_ra > NI_IPV6_ACCEPT_RA_ROUTER)
		accept_ra = NI_IPV6_ACCEPT_RA_ROUTER;

	return ni_format_uint_mapped(accept_ra, __accept_ra_names);
}

const char *
ni_ipv6_devconf_accept_dad_to_name(int accept_dad)
{
	static const ni_intmap_t	__accept_dad_names[] = {
		{ "disable",		NI_IPV6_ACCEPT_DAD_DISABLED	},
		{ "fail-address",	NI_IPV6_ACCEPT_DAD_FAIL_ADDRESS	},
		{ "fail-protocol",	NI_IPV6_ACCEPT_DAD_FAIL_PROTOCOL},
		{ NULL,			NI_IPV6_ACCEPT_DAD_DEFAULT	}
	};
	if (accept_dad < NI_IPV6_ACCEPT_DAD_DEFAULT)
		accept_dad = NI_IPV6_ACCEPT_DAD_DEFAULT;
	else
	if (accept_dad > NI_IPV6_ACCEPT_DAD_FAIL_PROTOCOL)
		accept_dad = NI_IPV6_ACCEPT_DAD_FAIL_PROTOCOL;

	return ni_format_uint_mapped(accept_dad, __accept_dad_names);
}

const char *
ni_ipv6_devconf_addr_gen_mode_to_name(int addr_gen_mode)
{
	static const ni_intmap_t	addr_gen_mode_map[] = {
		{ "eui64",		NI_IPV6_ADDR_GEN_MODE_EUI64		},
		{ "none",		NI_IPV6_ADDR_GEN_MODE_NONE		},
		{ "stable-privacy",	NI_IPV6_ADDR_GEN_MODE_STABLE_PRIVACY	},
		{ "random",		NI_IPV6_ADDR_GEN_MODE_RANDOM		},
		{ NULL,			NI_IPV6_ADDR_GEN_MODE_DEFAULT		}
	};
	return ni_format_uint_mapped(addr_gen_mode, addr_gen_mode_map);
}

static inline const char *
ni_ipv6_devconf_flag_to_sysctl_name(unsigned int flag)
{
	return ni_format_uint_mapped(flag, __ipv6_devconf_sysctl_name_map);
}

static int
__ni_ipv6_devconf_process_flag(ni_netdev_t *dev, unsigned int flag, int value)
{
	ni_ipv6_devinfo_t *ipv6 = dev->ipv6;
	unsigned int level;
	int unused = 0;

	switch (flag) {
	case NI_IPV6_DEVCONF_FORWARDING:
		ipv6->conf.forwarding = !!value;
		break;
	case NI_IPV6_DEVCONF_DISABLE_IPV6:
		ipv6->conf.enabled = !value;
		break;
	case NI_IPV6_DEVCONF_ACCEPT_REDIRECTS:
		ipv6->conf.accept_redirects = !!value;
		break;
	case NI_IPV6_DEVCONF_ACCEPT_RA:
		ipv6->conf.accept_ra = value < 0 ? 0 : value > 2 ? 2 : value;
		break;
	case NI_IPV6_DEVCONF_ACCEPT_DAD:
		ipv6->conf.accept_dad = value < 0 ? 0 : value > 2 ? 2 : value;
		break;
	case NI_IPV6_DEVCONF_AUTOCONF:
		ipv6->conf.autoconf = !!value;
		break;
	case NI_IPV6_DEVCONF_USE_TEMPADDR:
		ipv6->conf.privacy = value < -1 ? -1 : value > 2 ? 2 : value;
		break;
	case NI_IPV6_DEVCONF_ADDR_GEN_MODE:
		ipv6->conf.addr_gen_mode = value < 0 ? 0 : value;
		break;
	case NI_IPV6_DEVCONF_STABLE_SECRET:
		/* omitted via netlink using an int32_t array */
		unused = 2;
		break;
	default:
		/* TODO: handle more (all) of them */
		unused = 1;
		break;
	}

	level = NI_LOG_DEBUG1 + unused;
	if (unused < 2 && ni_debug_guard(level, NI_TRACE_EVENTS|NI_TRACE_IPV6)) {
		const char *name;

		name = ni_ipv6_devconf_flag_to_sysctl_name(flag);
		if (name) {
			ni_trace("%s[%u]: get ipv6.conf.%s = %d%s",
				dev->name, dev->link.ifindex, name,
				value, unused ? " (unused)" : "");
		} else {
			ni_trace("%s[%u]: get ipv6.conf.[%u] = %d%s",
				dev->name, dev->link.ifindex, flag,
				value, unused ? " (unused)" : "");
		}
	}
	return unused;
}

int
__ni_ipv6_devconf_process_flags(ni_netdev_t *dev, int32_t *array, unsigned int count)
{
	unsigned int i;

	if (!array || !dev || !ni_netdev_get_ipv6(dev))
		return -1;

	for (i = 0; i < count; ++i) {
		int32_t value = array[i];
		/*
		 * unlike ipv6 the flags start at 0 not at 1;
		 * we keep it same as defined in linux/ipv6.h.
		 */
		if (__ni_ipv6_devconf_process_flag(dev, i, value) < 0)
			return -1;
	}
	return 0;
}

