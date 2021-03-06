/*
 *  ebt_ip
 *
 *	Authors:
 *	Bart De Schuymer <bdschuym@pandora.be>
 *
 *  April, 2002
 *
 *  Changes:
 *    added ip-sport and ip-dport
 *    Innominate Security Technologies AG <mhopf@innominate.com>
 *    September, 2002
 */

#include <linux/netfilter_bridge/ebtables.h>
#include <linux/netfilter_bridge/ebt_ip.h>
#include <linux/ip.h>
#include <linux/in.h>
#include <linux/module.h>
#include <linux/if_vlan.h>

struct tcpudphdr {
	uint16_t src;
	uint16_t dst;
};

static int ebt_filter_ip(const struct sk_buff *skb, const struct net_device *in,
   const struct net_device *out, const void *data,
   unsigned int datalen)
{
	struct ebt_ip_info *info = (struct ebt_ip_info *)data;
	union {struct iphdr iph; struct tcpudphdr ports;} u;
    /* Start of macfilter support QinQ by c47036 20060714 */
    unsigned char* p8021q = skb->data;
    int ipoffset = 0;
    if (skb->protocol == __constant_htons(ETH_P_8021Q))
    {
        p8021q += 2;
        ipoffset = p8021q - skb->data;

        if (*(unsigned short*)p8021q == __constant_htons(ETH_P_PPP_SES))
        {
            if (*(unsigned short*)(p8021q + 2 + 6) == __constant_htons(0x0021))
            {
                p8021q += (2 + 6 + 2); // VLAN+PPP+IP
                ipoffset = p8021q - skb->data;
            }
            else
            {
                // 其他情况
		        return EBT_NOMATCH;
            }
        }
        else if (*(unsigned short*)p8021q == __constant_htons(ETH_P_IP))
        {
            p8021q += 2; // VLAN+IP
            ipoffset = p8021q - skb->data;
        }
#ifdef CONFIG_KPN_VLAN
        else if (*(unsigned short*)p8021q == __constant_htons(ETH_P_8021Q))
        {
            if (*(unsigned short*)(p8021q + 2 + 2) == __constant_htons(ETH_P_IP))
            {
                p8021q += (2 + VLAN_HLEN) ;   // VLAN+VLAN+IP
                ipoffset = p8021q - skb->data;
            }
            else if((*(unsigned short*)(p8021q + 2 + 2) == __constant_htons(ETH_P_PPP_SES)))
            {
                if(*(unsigned short*)(p8021q + 2 + VLAN_HLEN + 6) == __constant_htons(0x0021))
                {
                    p8021q += (2 + VLAN_HLEN + 6 + 2) ;   // VLAN+VLAN+PPP+IP
                    ipoffset = p8021q - skb->data;
                }
                else
                {
                    // 其他情况
		            return EBT_NOMATCH;
                }
            }
            else
            {
                // 其他情况
		        return EBT_NOMATCH;
            }
        }
#endif // CONFIG_KPN_VLAN
        else
        {
	        return EBT_NOMATCH;
        }
    }
    else if(skb->protocol == __constant_htons(ETH_P_PPP_SES))
    {
        if(*(unsigned short*)(p8021q + 6) == __constant_htons(0x0021))
        {
            p8021q += (6 + 2); // PPP+IP
            ipoffset = p8021q - skb->data;
        }
        else
        {
	        return EBT_NOMATCH;
        }
    }
    else if(skb->protocol == __constant_htons(ETH_P_IP))
    {
        ipoffset = 0;
    }
    else
    {
        return EBT_NOMATCH;
    }
    /* End of macfilter support QinQ by c47036 20060714 */
    
    // support Q && QinQ by c47036 20060714
	//if (skb_copy_bits(skb, 0, &u.iph, sizeof(u.iph)))
	if (skb_copy_bits(skb, ipoffset, &u.iph, sizeof(u.iph)))
		return EBT_NOMATCH;
	if (info->bitmask & EBT_IP_TOS &&
	   FWINV(info->tos != u.iph.tos, EBT_IP_TOS))
		return EBT_NOMATCH;
	if (info->bitmask & EBT_IP_SOURCE &&
	   FWINV((u.iph.saddr & info->smsk) !=
	   info->saddr, EBT_IP_SOURCE))
		return EBT_NOMATCH;
	if ((info->bitmask & EBT_IP_DEST) &&
	   FWINV((u.iph.daddr & info->dmsk) !=
	   info->daddr, EBT_IP_DEST))
		return EBT_NOMATCH;
	if (info->bitmask & EBT_IP_PROTO) {
		if (FWINV(info->protocol != u.iph.protocol, EBT_IP_PROTO))
			return EBT_NOMATCH;
		if (!(info->bitmask & EBT_IP_DPORT) &&
		    !(info->bitmask & EBT_IP_SPORT))
			return EBT_MATCH;
		//support Q & QinQ by c47036 20060714
		/*if (skb_copy_bits(skb, u.iph.ihl*4, &u.ports,
		    sizeof(u.ports)))*/
		if (skb_copy_bits(skb, ipoffset + u.iph.ihl*4, &u.ports,
		    sizeof(u.ports)))    
			return EBT_NOMATCH;
		if (info->bitmask & EBT_IP_DPORT) {
			u.ports.dst = ntohs(u.ports.dst);
			if (FWINV(u.ports.dst < info->dport[0] ||
			          u.ports.dst > info->dport[1],
			          EBT_IP_DPORT))
			return EBT_NOMATCH;
		}
		if (info->bitmask & EBT_IP_SPORT) {
			u.ports.src = ntohs(u.ports.src);
			if (FWINV(u.ports.src < info->sport[0] ||
			          u.ports.src > info->sport[1],
			          EBT_IP_SPORT))
			return EBT_NOMATCH;
		}
	}
	return EBT_MATCH;
}

static int ebt_ip_check(const char *tablename, unsigned int hookmask,
   const struct ebt_entry *e, void *data, unsigned int datalen)
{
	struct ebt_ip_info *info = (struct ebt_ip_info *)data;

	if (datalen != EBT_ALIGN(sizeof(struct ebt_ip_info)))
		return -EINVAL;
	if (e->ethproto != __constant_htons(ETH_P_IP) ||
	   e->invflags & EBT_IPROTO)
		return -EINVAL;
	if (info->bitmask & ~EBT_IP_MASK || info->invflags & ~EBT_IP_MASK)
		return -EINVAL;
	if (info->bitmask & (EBT_IP_DPORT | EBT_IP_SPORT)) {
		if (info->invflags & EBT_IP_PROTO)
			return -EINVAL;
		if (info->protocol != IPPROTO_TCP &&
		    info->protocol != IPPROTO_UDP)
			 return -EINVAL;
	}
	if (info->bitmask & EBT_IP_DPORT && info->dport[0] > info->dport[1])
		return -EINVAL;
	if (info->bitmask & EBT_IP_SPORT && info->sport[0] > info->sport[1])
		return -EINVAL;
	return 0;
}

static struct ebt_match filter_ip =
{
	.name		= EBT_IP_MATCH,
	.match		= ebt_filter_ip,
	.check		= ebt_ip_check,
	.me		= THIS_MODULE,
};

static int __init init(void)
{
	return ebt_register_match(&filter_ip);
}

static void __exit fini(void)
{
	ebt_unregister_match(&filter_ip);
}

module_init(init);
module_exit(fini);
MODULE_LICENSE("GPL");
