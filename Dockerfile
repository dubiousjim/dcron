# Build dcron
FROM alpine AS builder
COPY . /src
RUN apk add --no-cache gcc make musl-dev \
	&& cd /src \
	&& make \
	&& make install

# Build image
FROM alpine

RUN apk add --no-cache tini \
	&& rm -rf /var/spool/cron \
	&& install -o root -d -m0755 -g root /var/spool/cron \
	&& install -o root -d -m0755 -g root /etc/cron.d

# Copy dcron
COPY --from=builder /usr/local/sbin/crond /usr/local/sbin/crond
COPY --from=builder /usr/local/bin/crontab /usr/local/bin/crontab
COPY /docker-entrypoint.sh /docker-entrypoint.sh

# The entrypoint makes sure the cron directories exists.
ENTRYPOINT ["/docker-entrypoint.sh"]

# Start dcron
CMD ["/usr/local/sbin/crond", "-f", "-L", "-", "-l", "info"]

# Mark /etc/crontab as a volume so cron settings are saved
VOLUME /var/spool/cron

