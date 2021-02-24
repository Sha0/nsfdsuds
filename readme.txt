Author: Shao Miller <code@sha0.net>
Date: 2021-02-23

TODO: Expand upon this documentation with a specific tutorial.

NameSpace File-DescriptorS over a Unix Domain Socket.

Docker and Kubernetes are great technologies that enable many possibilities.
These software also introduce some limitations upon one's interface with the
underlying technologies, however; in particular, there is a focus on stateless
containers.  The documentation describes some parallels with virtual machines,
but if one really wishes to migrate a stateful VM, one might notice that
root filesystem persistence presents a major obstacle.

Consider the following scenario: one has a stateful LXC container which uses
an overlayfs as its root filesystem, and the top layer ("diff" or "delta") is
persistent.  If one wishes to enjoy the many, many niceties of Kubernetes, one
might then set about working upon a migration-strategy, but the aforementioned
obstacle should be obvious very early on.  The source-code in this project is
part of an attempt to [painfully] work around the obstacle.

Suppose one has the following StatefulSet:
---
apiVersion: apps/v1
kind: StatefulSet
metadata:
  name: nsfdsuds
  namespace: nsfdsuds
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
      - args:
        - -c
        - while true; do sleep 60; done
        command:
        - /bin/sh
        image: busybox
        name: busybox-priv
        resources: {}
        securityContext:
          privileged: true
        volumeMounts:
        - mountPath: /shared
          mountPropagation: Bidirectional
          name: shared
      - args:
        - -c
        - while true; do sleep 60; done
        command:
        - /bin/sh
        image: busybox
        name: busybox
        resources: {}
        volumeMounts:
        - mountPath: /shared
          mountPropagation: HostToContainer
          name: shared
      volumes:
      - emptyDir: {}
        name: shared
---

There is a privileged container and a non-privileged container.  They share a
common /shared mount-point and the mount-propagation specification allows for
manipulations of that mount-point within the privileged container to be
observable by the non-privileged container.

Further suppose that the non-privileged container uses nsfdsuds in
server-mode:

  # ./nsfdsuds --server /shared/nsfdsuds.socket

And that the privileged container uses nsfdsuds in client-mode:

  # ./nsfdsuds --client /shared/nsfdsuds.socket /bin/sh

The non-privileged container should pass its namespace file-descriptors across
the socket-file on the shared mount-point to the privileged container, who
will then execv to /bin/sh and retain these FDs.  From that point, the
privileged container may be able to use the nsenter utility (or setns
system-call) with these FDs to manipulate mount-points within the non-
privileged container, such as:

- Establishing an overlayfs with a persistent top layer

- Re-mounting / binding / moving mount-points established by the wonderful
Kubernetes + Docker combination into this overlayfs

- Informing the non-privileged container that it's time to chroot into the
newly-established overlayfs
