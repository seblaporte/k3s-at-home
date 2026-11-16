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

## Étape 4.3 : VLAN 4 à 7 sur le trunk et les bornes

Aucun appareil ne change de VLAN à cette étape : seuls les VLAN 4 à 7 sont ajoutés,
tagués, sur les ports qui les transportent.

- [ ] Créer les VLAN, avec ces ports **Tagged** et tous les autres **Not Member** :

  | VID | Nom        | Tagged                     |
  | --- | ---------- | -------------------------- |
  | 4   | multimedia | routeur (1), bornes (2, 3) |
  | 5   | security   | routeur (1), bornes (2, 3) |
  | 6   | iot        | routeur (1), bornes (2, 3) |
  | 7   | car        | routeur (1), bornes (2, 3) |

- [ ] Ne toucher ni au VLAN 1, ni aux VLAN 2 et 3, ni à aucun PVID.
- [ ] Contrôle sans enregistrer : le Mac (Wi-Fi) et le cluster répondent toujours
      (`network/openwrt/vlan.sh status`, `kubectl get nodes`), les invités ont Internet.
- [ ] Test d'un VLAN de bout en bout (facultatif) : passer un port libre (le 16 par
      exemple) en **Untagged** VLAN 4, PVID 4, y brancher un portable : il reçoit une
      adresse `192.168.4.15x` et a Internet, sans accès au VLAN 1 hors des règles prévues.
      **Remettre ensuite le port en VLAN 1** (Untagged 1, PVID 1, Not Member 4).
- [ ] Enregistrer la configuration (« Save Configuration »).
- [ ] Exporter la nouvelle configuration (hors du dépôt).

## Étape 4.6 : appareils filaires

À faire avec le groupe concerné, quand sa réservation DHCP existe (`vlan.sh apply
vlans`). Pour un port d'accès : **Untagged** dans le nouveau VLAN, **Not Member** du
VLAN 1, puis **PVID** = nouveau VLAN (dans cet ordre, sinon l'interface refuse un PVID
dont le port n'est pas membre non tagué).

- [ ] Groupe security : port du NVR (10) → VLAN 5. Débrancher et rebrancher le câble
      (ou redémarrer le NVR) pour qu'il renouvelle son bail : `192.168.5.10`.
- [ ] Groupe iot : port de la passerelle domotique (à repérer) → VLAN 6
      (`192.168.6.66`). Idem pour le Shelly s'il est filaire.
- [ ] Groupe car : port du Raspberry du véhicule (11) si filaire → VLAN 7.
- [ ] Après chaque port : vérifications de `doc/vlan-migration.md`, puis enregistrer.

Retour arrière d'un port : Untagged VLAN 1, PVID 1, Not Member du nouveau VLAN.
