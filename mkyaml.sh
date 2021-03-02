#!/bin/bash
cat << EOF
$(kubectl create configmap nsfdsuds-scripts --from-file=scripts --dry-run=client -o yaml)
---
apiVersion: v1
kind: ConfigMap
metadata:
  name: nsfdsuds-env
data:
  EXCLUDEDIR: "/exclude"
  INITFILE: "/exclude/scripts/init.sh"
  LOWERDIR: "/exclude/lowerdir"
  NEWROOT: "/exclude/newroot"
  NSENTER: "/exclude/scripts/nsenter.sh"
  NSFDSUDS: "/nsfdsuds"
  OVERLAYDIR: "/exclude/overlay"
  READYFILE: "/ready"
  REMOUNTFILE: "/exclude/scripts/remount.sh"
  SETUPFILE: "/exclude/scripts/overlay.sh"
  SOCKETFILE: "/exclude/shared/nsfdsuds.socket"
---
apiVersion: apps/v1
kind: StatefulSet
metadata:
  name: nsfdsuds
spec:
  replicas: 1
  selector:
    matchLabels:
      app: nsfdsuds
  serviceName: nsfdsuds
  template:
    metadata:
      labels:
        app: nsfdsuds
    spec:
      containers:
      - command:
        - /exclude/scripts/priv.sh
        envFrom:
        - configMapRef:
            name: nsfdsuds-env
        image: synthetel/busybox-nsfdsuds
        name: busybox-priv
        resources: {}
        securityContext:
          privileged: true
        volumeMounts:
        - mountPath: /exclude/scripts
          name: scripts
          readOnly: true
        - mountPath: /exclude/shared
          name: shared
      - command:
        - /exclude/scripts/nonpriv.sh
        envFrom:
        - configMapRef:
            name: nsfdsuds-env
        image: synthetel/busybox-nsfdsuds
        name: busybox-nonpriv
        resources: {}
        volumeMounts:
        - mountPath: /exclude/overlay
          name: overlay
        - mountPath: /exclude/scripts
          name: scripts
          readOnly: true
        - mountPath: /exclude/shared
          name: shared
      volumes:
      - emptyDir: {}
        name: overlay
      - configMap:
          name: nsfdsuds-scripts
          defaultMode: 0777
        name: scripts
      - emptyDir: {}
        name: shared
EOF
