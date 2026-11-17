# Checklist du switch D-Link DGS-1100-16V2

Le switch n'a ni API ni CLI : configuration à la main dans l'interface web
(`http://192.168.1.2`), en suivant cette liste. Les numéros de port viennent du plan de
`doc/vlan-migration.md` (section « Switch ») et **sont à confirmer sur le câblage réel**.

Règle d'or : **n'enregistrer la configuration (« Save Configuration ») qu'après
validation**. Tant qu'elle n'est pas enregistrée, un redémarrage du switch (débrancher
puis rebrancher) revient à la configuration précédente.

Libellés de l'interface web (firmware 2.x, à ajuster si le menu diffère) :
`VLAN > 802.1Q VLAN` pour l'appartenance, `VLAN > 802.1Q VLAN PVID` pour les PVID,
`Tools > Configuration Backup and Restore` pour l'export, `Save > Save Configuration`.

## Avant de commencer

- [ ] Câble disponible vers le port de secours du routeur (`eth2`, VLAN 1 non tagué,
      étape 4.1 faite).
- [ ] Exporter la configuration actuelle et la ranger **hors du dépôt**
      (`~/Backups/switch/dgs-1100-AAAAMMJJ.cfg`) : elle contient le mot de passe d'admin.
- [ ] Relever et reporter dans `doc/vlan-migration.md` (section « État constaté ») :
  - [ ] mode VLAN : « 802.1Q » attendu ; « Asymmetric VLAN » désactivé ;
  - [ ] VLAN existants (1, 2, 3 attendus) et appartenance de chaque port ;
  - [ ] PVID de chaque port ;
  - [ ] port de chaque équipement : routeur, bornes U6-LR et U6-Lite, Raspberry du
        cluster, NAS, NVR, Mac mini, passerelle domotique, Shelly (si filaire).

Attendu aujourd'hui : port du routeur tagué 1, 2 et 3 (le routeur envoie le VLAN 1
tagué) ; ports des bornes non tagués 1 et tagués 2 et 3.

## Étape 4.3 : déclarer les VLAN 4 à 7

Configuration constatée le 2026-11-16 (export) : **les 16 ports sont en trunk**,
VLAN 1 natif (non tagué), VLAN 2 à 4094 autorisés tagués. Seuls les VLAN 1, 2 et 3
sont déclarés ; un VLAN non déclaré est filtré. Il suffit donc de **déclarer** les VLAN
4 à 7 : aucun port ni PVID ne change, et le câblage n'a pas d'importance ici.

- [ ] Déclarer les VLAN (menu `VLAN > 802.1Q VLAN`, ajout d'un VID), sans toucher aux
      ports :

  | VID | Nom        |
  | --- | ---------- |
  | 4   | multimedia |
  | 5   | security   |
  | 6   | iot        |
  | 7   | car        |

- [ ] Vérifier sur la page des ports que tous restent trunk, natif 1, autorisés 2–4094.
- [ ] Contrôle sans enregistrer : le Mac et le cluster répondent toujours
      (`network/openwrt/vlan.sh status`, `kubectl get nodes`), les invités ont Internet.
- [ ] Enregistrer la configuration (« Save Configuration »), puis l'exporter (hors du
      dépôt).

Le passage de bout en bout (un client qui reçoit une adresse `192.168.4.x`) se vérifie
après l'étape 4.4, en déplaçant un premier appareil.

## Étape 4.6 : appareils filaires

À faire avec le groupe concerné, quand sa réservation DHCP existe (`vlan.sh apply
vlans`). Pour un port d'accès : **Untagged** dans le nouveau VLAN, **Not Member** du
VLAN 1, puis **PVID** = nouveau VLAN (dans cet ordre, sinon l'interface refuse un PVID
dont le port n'est pas membre non tagué). Avec ce firmware (ports en trunk) : passer le
port en mode **access** sur le nouveau VLAN.

- [ ] Groupe security : port du NVR (10) → VLAN 5. Débrancher et rebrancher le câble
      (ou redémarrer le NVR) pour qu'il renouvelle son bail : `192.168.5.10`.
- [ ] Groupe iot : port de la passerelle domotique (à repérer) → VLAN 6
      (`192.168.6.66`). Idem pour le Shelly s'il est filaire.
- [ ] Groupe car : port du Raspberry du véhicule (11) si filaire → VLAN 7.
- [ ] Après chaque port : vérifications de `doc/vlan-migration.md`, puis enregistrer.

Retour arrière d'un port : Untagged VLAN 1, PVID 1, Not Member du nouveau VLAN (ou
retour en trunk, natif 1, autorisés 2–4094).

## Fin de migration : durcissement

- [ ] Ports d'accès (Raspberry, NAS, Mac mini…) en mode **access** VLAN 1 : un trunk
      ouvert à tous les VLAN laisse un appareil filaire compromis envoyer des trames
      taguées dans n'importe quel VLAN, sans passer par le pare-feu. Seuls le port du
      routeur et ceux des bornes restent en trunk.
- [ ] Supprimer les communautés SNMP `public` / `private` (le service est désactivé).
