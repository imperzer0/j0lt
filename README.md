# j0lt DNS amplification (DDoS) attack tool
 ------------------------------------------------------------
 > * spl0its-r-us
 > * the-scientist@rootstorm.com
 ------------------------------------------------------------
 > Knowledge:
 > * https://datatracker.ietf.org/doc/html/rfc1700    (NUMBERS)
 > * https://datatracker.ietf.org/doc/html/rfc1035    (DNS)
 > * https://datatracker.ietf.org/doc/html/rfc1071    (CHECKSUM)
 > * https://www.rfc-editor.org/rfc/rfc768.html       (UDP)
 > * https://www.rfc-editor.org/rfc/rfc760            (IP)
 ------------------------------------------------------------
 * Usage: sudo ./j0lt -t &lt;target&gt; -p &lt;port&gt;
 * $ gcc j0lt.c -o j0lt
 * $ sudo ./j0lt -t 127.0.0.1 -p 80
 * ------------------------------------------------------------
 * Options:
 * [-x] will print a hexdump of the packet headers
 * [-d] puts j0lt into debug mode, no packets are sent
 * [-r list] will not fetch a resolv list, if one is provided.
 * [-m magnitude] request count
 ------------------------------------------------------------
 > What is DNS a amplification attack:
 > * A type of DDoS attack in which attackers use publicly
 > accessible open DNS servers to flood a target with DNS
 > response traffic. An attacker sends a DNS lookup request
 > to an open DNS server with the source address spoofed to
 > be the target’s address. When the DNS server sends the  
 > record response, it is sent to the target instead.
 ------------------------------------------------------------
