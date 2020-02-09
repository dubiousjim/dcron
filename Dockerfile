# Build dcron
FROM alpine AS builder
COPY . /src
RUN apk add --no-cache gcc make musl-dev \
	&& cd /src \
	&& make \
	&& make install

# Build image
FROM alpine

# Alpine includes a copy of dcron as part of busybox.
# Delete the /var/spool/cron dir and then recreate it.
RUN rm -rf /var/spool/cron \
# These are copied from the Makefiles
	&& install -o root -d -m0755 -g root /var/spool/cron \
	&& install -o root -d -m0755 -g root /etc/cron.d

# Copy dcron, crontab and the docker-entrypoint.sh
COPY --from=builder /usr/local/sbin/crond /usr/local/sbin/crond
COPY --from=builder /usr/local/bin/crontab /usr/local/bin/crontab

# The entrypoint makes sure the cron directories exists.
ENTRYPOINT ["/usr/local/sbin/crond"]

# Default options
CMD ["-C", "-f", "-L", "-"]

# Mark /var/spool/cron as a volume so cron settings are persisted there
VOLUME /var/spool/cron

