# Environnement ESPHome identique au cluster k3s, dans Docker.
#
# Même image (tag lu dans le HelmRelease de k3s-at-home, mis à jour par Renovate)
# et même organisation que le pod :
#   /config      lecture seule : devices/*.yaml + secrets.yaml
#   /components  lecture seule : composants locaux (components/<nom>/)
#   /data        ESPHOME_DATA_DIR : builds, état, composants externes clonés
#   /cache       toolchains PlatformIO (volume Docker persistant)
#
#   make help

SHELL := /bin/bash
.DEFAULT_GOAL := help

WORK := $(CURDIR)/.work
VERSION_URL := https://raw.githubusercontent.com/seblaporte/k3s-at-home/main/cluster/apps/esphome/helm-release.yaml
VERSION_CACHE := $(WORK)/esphome-version

# Version du serveur ; à défaut de réseau, dernière version connue
ESPHOME_VERSION ?= $(shell v=$$(curl -fsSL --max-time 5 $(VERSION_URL) 2>/dev/null \
	| sed -n 's/^ *tag: *"\{0,1\}\([0-9][0-9.]*\)"\{0,1\} *$$/\1/p' | head -1); \
	if [ -n "$$v" ]; then mkdir -p $(WORK); echo "$$v" > $(VERSION_CACHE); echo "$$v"; \
	elif [ -f $(VERSION_CACHE) ]; then cat $(VERSION_CACHE); fi)
IMAGE := esphome/esphome:$(ESPHOME_VERSION)

DEVICES := $(sort $(basename $(notdir $(wildcard devices/*.yaml))))
TTY := $(shell [ -t 0 ] && echo -it)
# Volume Docker nommé en local ; chemin d'un dossier en CI (mis en cache par actions/cache)
PIO_CACHE ?= esphome-cache

DOCKER_RUN = docker run --rm \
	-v $(WORK)/config:/config:ro \
	-v $(CURDIR)/components:/components:ro \
	-v $(WORK)/data:/data -e ESPHOME_DATA_DIR=/data \
	-v $(PIO_CACHE):/cache \
	$(1) $(IMAGE)

.PHONY: help version secrets stage validate compile upload run logs flash-usb dashboard kustomization check clean

help: ## Affiche cette aide
	@grep -hE '^[a-z-]+:.*## ' $(MAKEFILE_LIST) | awk -F':.*## ' '{printf "  make %-14s %s\n", $$1, $$2}'
	@echo
	@echo "  Variables : DEVICE=<nom> (sans .yaml)  IP=<adresse>  PORT=/dev/cu.usbserial-…  DUMMY=1"
	@echo "  Appareils : $(DEVICES)"

version: ## Version d'ESPHome utilisée (celle du serveur)
	@[ -n "$(ESPHOME_VERSION)" ] || { echo "Version introuvable (réseau ?) : ESPHOME_VERSION=…" >&2; exit 1; }
	@echo "$(ESPHOME_VERSION)"

secrets.yaml: secret.sops.yaml
	@sops -d secret.sops.yaml | python3 -c 'import sys,yaml; sys.stdout.write(yaml.safe_load(sys.stdin)["stringData"]["secrets.yaml"])' > $@.tmp
	@chmod 600 $@.tmp && mv $@.tmp $@
	@echo "secrets.yaml déchiffré (ignoré par Git)"

secrets: secrets.yaml ## Déchiffre secret.sops.yaml en secrets.yaml (clé PGP requise)

# DUMMY=1 : secrets factices (CI, ou validation sans clé PGP)
stage: $(if $(DUMMY),,secrets.yaml)
	@[ -n "$(ESPHOME_VERSION)" ] || { echo "Version d'ESPHome introuvable" >&2; exit 1; }
	@rm -rf $(WORK)/config && mkdir -p $(WORK)/config $(WORK)/data components
	@cp devices/*.yaml devices/.gitignore $(WORK)/config/
	@$(if $(DUMMY),python3 scripts/dummy-secrets.py > $(WORK)/config/secrets.yaml,cp secrets.yaml $(WORK)/config/)

define need_device
	@[ -n "$(DEVICE)" ] || { echo "Préciser DEVICE=<nom> parmi : $(DEVICES)" >&2; exit 1; }
	@[ -f devices/$(DEVICE).yaml ] || { echo "devices/$(DEVICE).yaml introuvable" >&2; exit 1; }
endef

validate: stage ## Valide tous les appareils (ou DEVICE=…)
	@echo "ESPHome $(ESPHOME_VERSION)"; fail=0; \
	for d in $(or $(DEVICE),$(DEVICES)); do \
	  if out=$$($(call DOCKER_RUN) config /config/$$d.yaml 2>&1); then echo "  OK    $$d"; \
	  else echo "  ÉCHEC $$d"; echo "$$out" | grep -E "ERROR|Failed|^\[" | head -5 | sed 's/^/        /'; fail=1; fi; \
	done; exit $$fail

compile: stage ## Compile DEVICE=…
	$(need_device)
	$(call DOCKER_RUN,$(TTY)) compile /config/$(DEVICE).yaml

upload: stage ## Envoie en OTA le dernier build : DEVICE=… IP=…
	$(need_device)
	@[ -n "$(IP)" ] || { echo "Préciser IP=… (le mDNS ne traverse pas Docker Desktop)" >&2; exit 1; }
	$(call DOCKER_RUN,$(TTY)) upload /config/$(DEVICE).yaml --device $(IP)

run: compile upload ## Compile puis envoie en OTA : DEVICE=… IP=…

logs: stage ## Logs en direct : DEVICE=… IP=…
	$(need_device)
	@[ -n "$(IP)" ] || { echo "Préciser IP=…" >&2; exit 1; }
	$(call DOCKER_RUN,$(TTY)) logs /config/$(DEVICE).yaml --device $(IP)

flash-usb: compile ## 1er flash par USB (hors Docker) : DEVICE=… PORT=/dev/cu.…
	@[ -n "$(PORT)" ] || { echo "Préciser PORT=… (ls /dev/cu.*)" >&2; exit 1; }
	@[ -x $(WORK)/venv/bin/esptool ] || { python3 -m venv $(WORK)/venv && $(WORK)/venv/bin/pip install -q esptool; }
	@bin=$$(ls $(WORK)/data/build/$(DEVICE)/.pioenvs/*/firmware.factory.bin 2>/dev/null | head -1); \
	[ -n "$$bin" ] || { echo "firmware.factory.bin introuvable pour $(DEVICE)" >&2; exit 1; }; \
	echo "Flash de $$bin sur $(PORT)"; \
	$(WORK)/venv/bin/esptool --port $(PORT) write-flash 0x0 "$$bin"

dashboard: stage ## Tableau de bord local sur http://localhost:6052 (lecture seule, comme sur k3s)
	$(call DOCKER_RUN,$(TTY) -p 6052:6052) dashboard /config

kustomization: ## Régénère kustomization.yaml après ajout/suppression de fichiers
	@python3 scripts/gen-kustomization.py

check: ## Vérifications de la CI (kustomization à jour, rendu kustomize)
	@python3 scripts/gen-kustomization.py --check
	@kubectl kustomize . > /dev/null && echo "kustomize build OK"

clean: ## Supprime l'environnement local (.work, secrets.yaml)
	rm -rf $(WORK) secrets.yaml
