# LibreSDR vaste TCXO: automatische TX-NCO

`overlay/usr/sbin/libresdr-tx-nco` corrigeert de TX-frequentie met de FPGA-NCO.
Het script draait met BusyBox `/bin/sh`, `awk` en `devmem`. Het schrijft niet
naar de TX-LO, sample-rate of `xo_correction` en start zelf geen zendsignaal.
Er is geen controle op het oude XO-script; zorg zelf dat dit niet meer bijregelt.

## Voorbereiden op de LibreSDR

1. Laad de aangepaste bitstream met NCO-registers `0x43c00020` t/m `0x43c00028`.
   De meegeleverde bitstream wordt door deze wijziging niet vervangen.
2. Gebruik een vaste 40 MHz TCXO en sluit de stabiele 10 MHz-referentie aan.
3. Zorg vooraf dat `ad9361-phy/xo_correction` op `40000000` staat. Het herstellen
   daarvan kan de radio onderbreken; de controller doet dit daarom niet zelf.
4. Zet `FPGA_NCO_SUPPORTED=1` in `/etc/libresdr-tx-nco.conf`. De FPGA heeft geen
   capability-register; deze instelling bevestigt expliciet de juiste bitstream.
5. Controleer dat `out_voltage_sampling_frequency` van `ad9361-phy` de rate op
   het TX-NCO-datapad is. Bij een afwijkende integratie kun je `TX_RATE_ATTR`
   instellen op het juiste converter-rate-attribuut, na interpolatie.

Start als root:

```sh
libresdr-tx-nco
```

Dit start de achtergrondregeling en opent het terminalscherm. `q` sluit alleen
het scherm; de regeling loopt door, ook na het sluiten van SSH. `d` stopt de
regeling en schakelt de NCO uit. Het scherm gebruikt ANSI-codes en BusyBox ash
`read -n -t`. Een minimale terminal van ongeveer 80 kolommen is aanbevolen.

```sh
libresdr-tx-nco start    # achtergrond zonder scherm
libresdr-tx-nco status  # eenmalige status
libresdr-tx-nco stop    # stoppen, laatste NCO-correctie blijft actief
libresdr-tx-nco disable # stoppen en NCO uitschakelen
libresdr-tx-nco run     # controller op de voorgrond, voor diagnose
libresdr-tx-nco self-test # rekentest met de lokale awk, zonder hardwarewrites
```

`/run/libresdr-tx-nco/status` bevat de laatste status, `events` de laatste acht
gebeurtenissen en `daemon.log` opstartfouten. Na stoppen is de status een laatste
momentopname. Het PID/lock voorkomt alleen dubbele exemplaren van deze controller.
Na een harde proceskill moet een achtergebleven lock handmatig verwijderd worden,
nadat gecontroleerd is dat de bijbehorende PID niet meer draait.

## Regelgedrag

- Iedere seconde: referentiestatus, TX-LO en converter-rate lezen.
- Standaard iedere twee seconden: signed TCXO-fout bemonsteren. De FPGA biedt
  geen sequence-counter, dus individuele nieuwe meetvensters zijn niet bewijsbaar
  te herkennen. Twee seconden is een conservatieve startwaarde; identieke of nul
  metingen worden niet ten onrechte als defect aangemerkt.
- Na start of referentieherstel eerst twee seconden wachten en minstens vijf
  samples verzamelen. Het venster groeit vervolgens tot zestien samples.
- Het gemiddelde van het schuivende venster bepaalt de correctie. Bij een
  spreiding boven 5 Hz of een absolute meetfout boven 10000 Hz geen nieuwe schatting.
- Vanaf vijf samples `TRACKING`; bij een vol geldig venster `STABLE`. Deze status
  betekent een gevuld geldig filter, geen bewezen thermische stabiliteit of PLL-lock.
- Alleen toepassen bij minstens 2 Hz verschil op de TX-uitgang, bij eerste
  inschakeling of bij een gewijzigde LO/rate. Geen wachttijd van vijf minuten.
- Nieuwe TX-FTW en RX-FTW=0 schrijven, daarna apply-bit omkeren. Geen fase-reset.
  Registerreadback controleert de geschreven configuratie, niet onafhankelijk de
  verwerking in het datapad. Bij een schrijffout blijven verdere writes geblokkeerd
  tot een herstart; de toestand kan dan gedeeltelijk toegepast zijn.
- Zonder referentie `WAITING`, of `HOLDOVER` als een geldige schatting bestaat.
  In holdover blijft de FTW behouden; alleen een LO/ratewissel herberekent met
  de laatste geldige TCXO-fout. Na herstel wachten op nieuwe geldige samples.
- Bij gewijzigde `xo_correction` blokkeren verdere applies tot herstart.

De formule gebruikt `e = gemeten TCXO - 40000000`:

```text
actual_fs = tx_fs * (40000000 + e) / 40000000
shift     = -tx_lo * e / 40000000
tx_ftw    = round(shift * 2^32 / actual_fs)
```

De correctie richt zich op het TX-centrum. Een NCO herstelt de fysieke sample-rate
niet: bij signalen buiten het centrum blijft de kleine proportionele timing- en
frequentiefout bestaan. De interne AXI-DDS omzeilt deze NCO; gebruik het DMA/DVB-pad.
RX blijft uit. De oude analoge lock-bit is geen vereiste; `ref_present` is dat wel.
Wijzig tijdens gebruik de klokbron/DAC-regeling niet via andere software.

## Firmware en boot

De Libre-defconfig voegt `board/tezuka/libre/overlay` toe na de gedeelde overlays
uit `board/tezuka/common`. De Libre
post-buildstap zet de uitvoerrechten, ook voor builds uit een Windows-checkout.
Voor een bestaande Buildroot-output moet de bijgewerkte defconfig opnieuw geladen
worden voordat de firmware wordt gebouwd.

Voor starten bij boot: zet ook `AUTOSTART=1` in de configuratie. `S95tx-nco`
start de achtergrondregeling; open later het scherm via SSH. Standaard staat boot
uit omdat Libre-boards ook andere oscillatoren/bitstreams kunnen hebben. Wijzig
de configuratie in de bron-overlay voor instellingen die in het firmware-image
moeten zitten; wijzigingen in een vluchtig rootfs overleven een reboot niet.

Voor testen zonder nieuwe firmware kun je het script en de configuratie naar
`/tmp` op het board kopiëren:

```sh
export NCO_CONFIG=/tmp/libresdr-tx-nco.conf
sh /tmp/libresdr-tx-nco
```

## Validatie

Vanaf de repository-root, in een POSIX-omgeving:

```sh
sh board/tezuka/libre/tests/tx-nco-smoke.sh
```

De test gebruikt een tijdelijke nep-registerbank en IIO-bestanden. Op hardware
blijven de tekenrichting, juiste TX-rate, koude start, LO/ratewissels, 10 MHz
verlies/herstel en fasecontinuïteit tijdens zenden te verifiëren. Gebruik voor
de RF-meting dezelfde 10 MHz-tijdbasis. Filter en drempel zijn startwaarden;
2 Hz is een apply-drempel, geen gegarandeerde absolute nauwkeurigheid.
