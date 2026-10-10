# VolSync + restic : drill de restauration (PVC iSCSI)

Procédure pour vérifier qu'une sauvegarde VolSync/restic d'un PVC `synology-iscsi-storage`
est réellement restaurable, sans toucher au PVC de production. Validée le 2026-10-09 sur le
pilote `uptime-kuma` (voir #1008, #1009, #1010, #1011).

## Pièges connus (rencontrés pendant la mise en place)

| Symptôme                                                             | Cause                                                                                                                                                                  | Fix                                                                                  |
| --------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | --------------------------------------------------------------------------------------- |
| `VolumeSnapshot` jamais `readyToUse`, erreur en boucle `sourceVolumeMode is required once set` | Le manifeste `setup-snapshot-controller.yaml` vendored depuis le tag `v8.4.0` d'external-snapshotter pointe encore vers l'image `v8.2.1`, incompatible avec le champ `sourceVolumeMode` des CRDs `v8.4.0` | Forcer l'image à `v8.4.0` (voir `cluster/core/snapshot-controller/kustomization.yaml`, bloc `images:`) |
| Même erreur après le fix ci-dessus, cette fois côté sidecar Synology   | Le sidecar `csi-snapshotter` vendored par Synology est pinné à `v4.2.1` (2021), antérieur au champ `sourceVolumeMode`                                                    | Bump vers `v8.4.0` dans `cluster/core/synology-csi-snapshotter/snapshotter.yaml`       |
| `cannot patch resource "volumesnapshotcontents" ... at the cluster scope` | Le sidecar `v8.4.0` utilise des requêtes `PATCH`, le `ClusterRole` vendored par Synology (pensé pour `v4.2.1`, qui ne fait que des `UPDATE`) n'autorise pas ce verbe        | Ajouter `patch` aux verbes du `ClusterRole synology-csi-snapshotter-role`               |
| Le `VolumeSnapshot` ne se crée/supprime pas tout de suite              | Le controller ne retraite un objet que sur événement ou resync périodique (~15 min) ; pas de retry immédiat après un fix RBAC/image                                     | `kubectl annotate volumesnapshotcontent <name> force-resync=$(date +%s) --overwrite` pour forcer un nouvel événement |
| Le PVC restauré disparaît juste après la fin du job de restauration    | `cleanupTempPVC: true` sur un `ReplicationDestination` avec `copyMethod: Direct` nettoie aussi le PVC de destination final, pas seulement un volume temporaire           | Ne **pas** mettre `cleanupTempPVC` avec `copyMethod: Direct` ; `cleanupCachePVC: true` reste correct et utile |
| Les volumes de test restent alloués sur le NAS après `kubectl delete pvc` | `synology-iscsi-storage` a `reclaimPolicy: Retain` : supprimer le PVC détache le PV mais ne supprime jamais le LUN sous-jacent                                           | Voir section nettoyage ci-dessous                                                       |

## Procédure

Remplacer `<ns>`, `<app>`, `<pvc-secret>` (le `Secret` restic de l'app, ex.
`slskd-volsync-restic`) selon l'app testée.

### 1. Lancer la restauration dans un PVC neuf (jamais sur le PVC de prod)

```bash
cat <<EOF | kubectl apply -f -
apiVersion: volsync.backube/v1alpha1
kind: ReplicationDestination
metadata:
  name: <app>-restore-test
  namespace: <ns>
spec:
  trigger:
    manual: restore-once
  restic:
    repository: <pvc-secret>
    copyMethod: Direct
    capacity: 1Gi             # >= taille du PVC source
    accessModes:
      - ReadWriteOnce
    storageClassName: synology-iscsi-storage
    cleanupCachePVC: true     # OK avec Direct
    # cleanupTempPVC: true    # NE PAS mettre avec copyMethod: Direct (voir piège ci-dessus)
EOF
```

Suivre l'avancement :

```bash
kubectl get replicationdestination -n <ns> <app>-restore-test -o jsonpath='{.status}' | jq
```

`status.latestMoverStatus.result` doit passer à `Successful`, et
`status.latestImage.name` donne le nom du PVC restauré
(`volsync-<app>-restore-test-dest`).

### 2. Inspecter les données restaurées

```bash
cat <<EOF | kubectl apply -f -
apiVersion: v1
kind: Pod
metadata:
  name: restore-check
  namespace: <ns>
spec:
  restartPolicy: Never
  containers:
    - name: check
      image: busybox:1.36
      command: ["sh", "-c", "sleep 300"]
      volumeMounts:
        - name: data
          mountPath: /data
  volumes:
    - name: data
      persistentVolumeClaim:
        claimName: volsync-<app>-restore-test-dest
EOF

kubectl exec -n <ns> restore-check -- sh -c "ls -la /data && du -sh /data"
```

Vérifier que les fichiers attendus de l'application sont présents (taille, noms, dates
cohérentes avec le dernier backup).

### 3. Nettoyer

```bash
kubectl delete pod -n <ns> restore-check
kubectl delete replicationdestination -n <ns> <app>-restore-test
kubectl delete pvc -n <ns> volsync-<app>-restore-test-dest
```

### 4. Libérer les volumes sur le NAS (reclaimPolicy: Retain)

Les commandes ci-dessus ne suppriment **pas** les LUN sur le NAS. Repérer les `PV`
orphelins laissés par le drill (destination + cache restic) :

```bash
kubectl get pv | grep -- "-restore-test"
```

Pour chaque `PV` listé, forcer la suppression réelle du volume backend (uniquement pour
des volumes de test — jamais sur un PV qui pourrait contenir des données utiles) :

```bash
kubectl patch pv <pv-name> -p '{"spec":{"persistentVolumeReclaimPolicy":"Delete"}}'
kubectl delete pv <pv-name>
```

Le changement de `reclaimPolicy` déclenche l'appel `DeleteVolume` réel sur le driver
`csi.san.synology.com`, qui supprime le LUN sur le NAS.
