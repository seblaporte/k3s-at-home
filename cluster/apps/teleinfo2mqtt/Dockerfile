FROM alpine:3.21
RUN apk add nodejs npm python3 make gcc g++ linux-headers udev
RUN mkdir /app
ADD index.js /app
ADD config.js /app
WORKDIR /app
RUN npm install raspi raspi-serial mqtt
CMD ["node","index.js"]
