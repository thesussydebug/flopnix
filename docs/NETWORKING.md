# Ethernet and IPv4

Open **Settings > Network**. Start with **Automatic (DHCP)** and **Automatic** adapter selection. Save changes. The Overview shows the detected card, address, subnet, router, DNS and remaining lease. Reconnect requests an address in the background; you can keep using the desktop. Test router checks the local connection, not Internet access.

A 10.x.x.x or 172.16-31.x.x address can be just as valid as 192.168.x.x. FLOPNIX uses the address supplied by the DHCP server. QEMU user networking normally supplies 10.0.2.15; it is a separate network from your home LAN. To test 192.168 addresses, use QEMU user networking with net=192.168.76.0/24,dhcpstart=192.168.76.100.

## Manual setup

Choose **IPv4 & DNS > Manual address** and enter values for your network. For example, on a router using 192.168.1.1/24, an available address might be 192.168.1.50, mask 255.255.255.0 and router 192.168.1.1. Choose an address outside its DHCP pool or reserve it on the router. Do not copy this example onto a different subnet.

The router and DNS fields may be blank for a local-only network. A DNS server translates names into addresses; a backup is tried if the first server does not answer. Automatic mode can also use your own DNS servers. Settings rejects malformed addresses, noncontiguous masks, network/broadcast host addresses and routers on the wrong subnet. Ctrl+A clears the focused field; Tab moves between editable fields; Ctrl+S saves.

## Adapter options

The Adapter page provides the card family, DHCP computer name, ISA I/O port and packet size (MTU). Defaults suit most setups. Adapter and ISA port changes take effect after restart. An ISA port of 0 probes 0x300 and 0x280; otherwise enter the card's configured hexadecimal port, such as 320. FLOPNIX polls the NIC and does not configure ISA Plug and Play cards. Packet size accepts 1280-1500 bytes; 1500 is normal Ethernet and 1492 can suit PPPoE paths.

| Family | Devices recognized | Verification |
| --- | --- | --- |
| NE2000 PCI | Realtek RTL8029; compatible Winbond, Compex, KTI, NetVin, VIA, SureCom and Winbond 89C940F IDs | QEMU ne2k_pci; clone hardware needs testing |
| NE2000 ISA | NE2000-compatible cards at configured I/O ports | QEMU ne2k_isa |
| Realtek RTL8139 | 10ec:8139; D-Link DFE-530TX+ 1186:1300 | QEMU rtl8139; D-Link hardware needs testing |
| DEC / ADMtek Tulip | DEC 21143; ADMtek AN981/AN985, including NC100 variants | QEMU tulip; existing ADMtek support retained |
| AMD PCnet PCI | AMD 1022:2000 PCnet family, 32-bit DMA rings | QEMU pcnet; physical PCnet variants need testing |

One adapter is active at a time. Automatic detection searches PCI buses, including behind bridges, before the safe ISA probes. Selecting a family helps when several cards are installed. This does not add drivers for every 1990s Ethernet card: 3Com EtherLink III, Intel EtherExpress and other unrelated controllers remain unsupported.

## What is supported

IPv4 over Ethernet, ARP, ICMP echo, DHCP, DNS, SNTP and the existing TCP/plain HTTP client. DHCP retries unanswered requests, renews and rebinds leases, withdraws expired or rejected addresses, and reacquires automatically. Manual settings, custom DNS, hostname, MTU and adapter choices persist in the existing configuration sector. ARP entries expire; malformed DHCP and damaged ICMP/UDP/TCP replies are rejected. UDP's optional zero checksum remains valid.

This is a small IPv4 stack: no IPv6, VLAN tagging, IP fragment reassembly, Wi-Fi, TLS/HTTPS, routing between interfaces or automatic link-local address allocation. The MTU limits transmitted IP packets; it is not path-MTU discovery. Hardware tests are still necessary for individual card revisions and their transceiver modes.

## Troubleshooting

- No adapter: use the Adapter page, then run lspci and netdiag in Terminal. For ISA, check the card's jumper/setup utility port and use the same value in Settings.
- Waiting for an address: check the Ethernet connection and that the network has a DHCP server. Reconnect retries; FLOPNIX will not fabricate a working address.
- Router responds but names fail: check DNS or choose a known DNS server reachable through your router.
- A remote host does not answer ping: it may block ICMP. A successful router test only proves local reachability.

netdiag reports I/O base, MAC provenance, link information when the card exposes it, DHCP state, counters and driver registers. ipconfig shows the current IPv4 values.

## Developer checks

Build normally, then compile the unshipped probe with bash tools/mkkext.sh kexts/nettest.c. Run python tools/nettests.py --nic pcnet (also ne2k_pci, ne2k_isa, rtl8139 and tulip). The runner copies built/flopnix.img and captures Ethernet traffic under out/nettests.

Additional cases: --static --subnet 172.16.0.0/16, --memory 4, --bridge (six PCI bridges), --isa-io 0x320 with ne2k_isa, --lifecycle (dropped discovery, renewal, rebind, expiry and NAK), and --offline (no DHCP server). Run GUI/selftest and network QEMU jobs sequentially because the older GUI/selftest runners stop all QEMU processes.

NetPrefs uses versioned FCfg.reserved bytes, leaving the 512-byte configuration sector and KAPI 31 layout unchanged. Additional net_get/net_set selectors are optional service capabilities. pcnet.inc reuses the RTL packet storage because only one NIC is active. Preserve DMA alignment and compiler ordering around ownership bits. netconfig.inc and dhcp_core.inc provide pure validation/packet tests in selftest.c.

Driver register/ID references: [QEMU PCnet](https://github.com/qemu/qemu/blob/master/hw/net/pcnet.c), [Linux NE2K PCI](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/8390/ne2k-pci.c), and [iPXE PCnet32](https://qemu.googlesource.com/ipxe/+/refs/heads/asn1fix/src/drivers/net/pcnet32.c).
