Zde je aktualizovaná dokumentace s přidanými poznámkami o zpoždění pro UDP multicast a DHCP fallback:

```markdown
# Komunikacni protokol pro napajeci jednotku (PSU) - PSU V2

Tato dokumentace popisuje komunikaci mezi ROS softwarem a mikrokontrolerem
W55RP20 EVB pro napajeci jednotku PSU. Aktualni navrh rozdeluje rizeni PSU na
dva nezavisle kanaly:

1. **TCP request-response** pro zapinani a vypinani power socketu.
2. **UDP multicast telemetry** pro prubezne posilani proudu a napeti.

---

## 1. Sitove nastaveni PSU

Preferovana varianta je DHCP:

- W55RP20 po startu pozada DHCP server o IP adresu.
- Po ziskani adresy zacne prijimat TCP prikazy a vysilat UDP multicast telemetrii.
- Pokud DHCP nebude dostupne, je mozne prejit na statickou IP adresu podle
  hlavni sitove dokumentace.

**Důležité časování při startu:**

- Po zapnutí zařízení je nutné počkat **10 sekund** na získání IP adresy od DHCP serveru.
- Pokud během této doby DHCP neodpoví, zařízení automaticky přejde na **fallback statickou IP adresu**.
- Fallback IP adresa: `172.16.10.20`
- Toto zpoždění zajišťuje, že síťové rozhraní je plně funkční před spuštěním dalších služeb.

Doporucena pevna identita v rover siti:

| Parametr | Hodnota |
| --- | --- |
| Zarizeni | `PICO-PSU` |
| Hostname | `pico-psu.perun-gen3.lan` |

---

## 2. TCP Request-Response pro power sockety

TCP se pouziva pro diskretni prikazy zapnuti a vypnuti power socketu.

Komunikaci vzdy iniciuje ROS:

- ROS otevre TCP spojeni na PSU.
- ROS odesle jeden `REQ` byte.
- W55RP20 provede nebo vyhodnoti prikaz.
- W55RP20 odesle jeden `RESP` byte.
- TCP spojeni se ukonci.

TCP spojeni neni persistentni a nepouziva se `keep-alive`.

### 2.1 TCP nastaveni

| Parametr | Hodnota |
| --- | --- |
| Protokol | TCP |
| Port | `50000` |
| Smer | `ROS -> PICO-PSU -> ROS` |
| Typ komunikace | Request-response |
| Persistentni spojeni | Ne |
| Keep-alive | Ne |

### 2.2 Format TCP requestu

Request ma velikost **1 byte**.

| Bity | Vyznam |
| --- | --- |
| 7-4 | Kod prikazu `CMD` |
| 3-0 | Argument prikazu `ARG` |

Obecne:

```text
REQ = (CMD << 4) | ARG
```

### 2.3 Format TCP response

Response ma velikost **1 byte**.

| Bity | Vyznam |
| --- | --- |
| 7-4 | Echo kodu prikazu `CMD` |
| 3-0 | Vysledek provedeni prikazu |

Dolni 4 bity odpovedi:

| Stav | Hodnota | Vyznam |
| --- | --- | --- |
| OK | puvodni argument + `8` | Prikaz byl prijat / proveden |
| FAIL | `0` | Prikaz selhal |

Obecne:

```text
RESP_OK   = (CMD << 4) | (ARG + 8)
RESP_FAIL = (CMD << 4) | 0
```

### 2.4 TCP prikazy pro power sockety

| Nazev | CMD | ARG | Popis |
| --- | --- | --- | --- |
| `POWER_SOCKET_ON` | `0001` | `0`-`7` | Zapne power socket 1 az 8 |
| `POWER_SOCKET_OFF` | `0010` | `0`-`7` | Vypne power socket 1 az 8 |

Kodovani socketu:

| Socket | ARG |
| --- | --- |
| 1 | `0` |
| 2 | `1` |
| 3 | `2` |
| 4 | `3` |
| 5 | `4` |
| 6 | `5` |
| 7 | `6` |
| 8 | `7` |

Toto kodovani je zvolene proto, aby slo jednoznacne rozlisit `OK` a `FAIL`.
Pri kodovani socketu jako `1`-`8` by odpoved `OK` pro socket 8 po pricteni `8`
pretekla v dolnich 4 bitech na `0`, tedy stejnou hodnotu jako `FAIL`.

Priklady:

| Request | Vyznam | Response OK | Response FAIL |
| --- | --- | --- | --- |
| `0001 0000` | Zapnout socket 1 | `0001 1000` | `0001 0000` |
| `0001 0111` | Zapnout socket 8 | `0001 1111` | `0001 0000` |
| `0010 0000` | Vypnout socket 1 | `0010 1000` | `0010 0000` |
| `0010 0111` | Vypnout socket 8 | `0010 1111` | `0010 0000` |

---

## 3. UDP Multicast Telemetry

UDP multicast se pouziva pro prubezne posilani telemetrie z PSU do ROS.

Telemetry tok jde smerem:

```text
PICO-PSU -> ROS
```

ROS se pripoji do odpovidajici multicast skupiny a prijima telemetry packety.

### 3.1 Multicast nastaveni

| Parametr | Hodnota |
| --- | --- |
| Multicast group | `239.192.1.100` |
| Port | `49151` |
| Protokol | UDP multicast |
| Smer | `PICO-PSU -> ROS` |
| Frekvence | Dle nastaveni Pico |
| Endianness | Little-Endian |

**Důležité omezení pro odesílání telemetrie:**

- Aby nedocházelo k zahlcení sítě a ROS systému, je třeba dodržet **minimální interval mezi jednotlivými UDP zprávami**.
- **Doporučený interval:** **1 sekunda** mezi jednotlivými telemetrickými zprávami.
- To znamená, že PSU nebude odesílat telemetrii častěji než jednou za sekundu. 
- Toto opatření zajišťuje stabilní přenos a předchází ztrátám paketů v síti.

### 3.2 Format UDP telemetry packetu

Packet ma pevnou velikost **40 bajtu**.

Data jsou odesilana jako binarni blok v tomto poradi:

| Poradi | Datovy typ | Velikost | Nazev | Popis |
| --- | --- | --- | --- | --- |
| 1 | `float[5]` | 20 B | `current_cells_1_5` | Proud na clancich 1 az 5, kazdy clanek samostatne |
| 2 | `float` | 4 B | `current_cells_6_8` | Proud na clancich 6 az 8 spolecne |
| 3 | `float` | 4 B | `voltage_cells_1_5` | Napeti na clancich 1 az 5 spolecne |
| 4 | `float` | 4 B | `voltage_cells_6_8` | Napeti na clancich 6 az 8 spolecne |
| 5 | `float` | 4 B | `current` | Celkovy output proud |
| 6 | `float` | 4 B | `state_of_charge` | Procentualni nabiti baterky; cislo 0.0 az 1.0 |

Celkovy output voltage je stejny jako `voltage_cells_1_5`
```

---

## Shrnutí přidaných změn:

1. **Do sekce 1 (Síťové nastavení)** přidáno:
   - Po startu čekat 10s na DHCP
   - Fallback IP adresa `172.16.10.20`
   - Popis, že teprve po této době se spouští ostatní služby

2. **Do sekce 3.1 (Multicast nastavení)** přidáno:
   - Omezení minimálního intervalu 1s mezi UDP zprávami
   - Zdůvodnění (zabránění zahlcení sítě, stabilita přenosu)