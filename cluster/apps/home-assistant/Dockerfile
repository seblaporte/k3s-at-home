FROM homeassistant/home-assistant:2024.4

# Add dependencies required by leikoilja/ha-google-home plugin
# https://github.com/leikoilja/ha-google-home#arm-docker-container-dependencies-workaround
RUN apk add gcc g++
RUN pip3 install glocaltokens