Author: Shao Miller <code@sha0.net>
Date: 2021-02-23

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

Included in this source-code is a demonstration of such a work-around by using
nsfdsuds.  After installing a statically-linkable GLibC, the nsfdsuds command
can be built with the -static flag to GCC:

  gcc -static -ansi -pedantic -Wall -Wextra -Werror -o nsfdsuds nsfdsuds.c

The demonstration uses BusyBox, else a static build wouldn't be as important.

The Dockerfile can be used to build a BusyBox image that includes nsfdsuds:

  docker build .

For the demonstration, this image has already been "pushed" to Docker Hub.

The mkyaml.sh script can be used to generate some Kubernetes YAML.  The
kubectl command is required.

  ./mkyaml.sh > demo.yaml

This YAML can then be deployed:

  kubectl -n <namespace> apply -f demo.yaml

The result is a StatefulSet which creates a pod with two containers:
- A non-privileged container
- A privileged container

These containers share a common mount-point: /exclude/shared/

The non-privileged container will use nsfdsuds in "server mode" and it will
listen on the /exclude/shared/nsfdsuds.socket socket-file.  The privileged
container will connect to the same socket-file and attempt to receive file-
descriptors for some of the namespaces from the non-privileged container.
These scripts are nonpriv.sh and priv.sh, respectively.

Once the privileged container has the NS FDs, it will execute the nsenter.sh
script, which finds the mount NS for the non-privileged container and then
executes the overlay.sh script in that namespace.  This latter will establish:
- /exclude/lowerdir/         : A "lower dir"
- /exclude/overlay/upperdir/ : An "upper dir"
- /exclude/overlay/workdir/  : A "work dir"
- /exclude/newroot/          : An overlayfs composed of these

In "real life" (outside of this demonstration), the "lower dir" would most
likely be a pod-mounted volume to some read-only, base filesystem image.  In
that case, the noted line in overlay.sh would be removed.  For the sake of
completeness, this demonstration simply bind-mounts the BusyBox image to the
"lower dir" to pretend that a full OS image is present, there.

Also in "real life," the /exclude/overlay/ directory would most likely be a
pod-mounted volume: where the persistent data is desired to be.  In this
demonstration, an emptyDir volume is used, so there won't actually be any
persistence across restarts.  Simply replace the emptyDir volume with a
persistent volume in order to enjoy real persistence.

After establishing the overlayfs, the overlay.sh script then invokes the
remount.sh script, which will bind-mount the many mount-points that were
established by Kubernetes and Docker, including /dev/ and /sys/ and so on.
These new mount-points will be in the overlayfs, at which point the overlayfs
will be suitably-prepared for a chroot operation.

The final work of the privileged container's overlay.sh script is to create a
"ready file" to note the completion of work.

Back in the non-privileged container, it has been waiting for this "ready
file" to exist.  Once the file is found, this container's nonpriv.sh script
will exec and chroot into the overlayfs established by the privileged
container.  In "real life" and for a full OS image at the overlayfs, this
would likely invoke /sbin/init instead of the simple init.sh script of the
demonstration.

In summary, the non-privileged container and the privileged container use
nsfdsuds to allow the privileged container to manipulate the mount-points of
the non-privileged container, set up an overlayfs and other important
mount-points, then the non-privileged container gives up its "old" root FS
space and uses the overlayfs.

One will notice, however, that using kubectl like this:

  kubectl -n namespace exec -it nsfdsuds-0 -c busybox-nonpriv -- /bin/sh

will result in a shell in the "old" root FS of the non-privileged container.
If one wishes to enter the overlayfs environment, one would then:

  exec chroot /path/to/newroot/

BONUS DOCUMENTATION

1. A previous iteration of this documentation included the following portions
of the StatefulSet:
[...]
        name: busybox-priv
[...]
        volumeMounts:
        - mountPath: /shared
          mountPropagation: Bidirectional
          name: shared
[...]
        name: busybox
[...]
        volumeMounts:
        - mountPath: /shared
          mountPropagation: HostToContainer
          name: shared
[...]

The idea for this is that the privileged container can manipulate the shared
mount-point and those manipulations will be accessible by the non-privileged
container.  Although an overlayfs could certainly be established in this way,
it doesn't gain much because the privileged container still needs to bind-
mount the many mount-points established by Kubernetes and Docker, for which it
will step inside the mount namespace of the non-privileged container, anyway.

2. During development of the demonstration, this error was encountered:

  $ kubectl -n namespace exec -it nsfdsuds-0 -c busybox-nonpriv -- /bin/sh
  OCI runtime exec failed: exec failed: container_linux.go:370: starting
  container process caused: read init-p: connection reset by peer: unknown
  command terminated with exit code 126

There was also a related Docker-log on the node:

  $ journalctl -xeu docker
  [...]
  [...] level=error msg="stream copy error: reading from a closed fifo"
  [...]

The cause was an accidental bind-mount of the / directory onto the / directory
(again) inside the non-privileged container.  Doing so made the many mount-
points established by Kubernetes and Docker inaccessible to whichever agent
was trying to establish a new command inside the container.

FINAL THOUGHT

If you enjoy this work, it would be great to express your enjoyment by sharing
it with others who might have similar goals.
