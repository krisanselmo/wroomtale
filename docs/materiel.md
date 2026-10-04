# Matériel

Source du plan : [`hardware/wiring.html`](../hardware/wiring.html) — détail bus
par bus, pièges, GPIO libres. Les PNG sont rendus par
[une action GitHub](../.github/workflows/wiring.yml) à chaque modification de la
page : ne pas les retoucher à la main.

## Choix des broches

SPI, I2C et I2S passent par la matrice GPIO : les numéros sont libres, seul
l'ordre sur le header compte. Côté droit, les signaux occupent les positions 6
à 13 — microSD sur 6, 8, 9, 10 ; PN532 sur 11, 12, 13 ; masse en position 7.

- GPIO 16 et 17 disponibles : ce module n'a pas de PSRAM.
- GPIO 34-39 évités pour les boutons : entrée seule, pas de pull-up interne.
- GPIO 0/2/12/15 évités : strapping pins.
- L'`IRQ` du PN532 n'est pas câblée : en I2C la bibliothèque interroge le
  registre d'état.

## Implantation sur Perma-Proto

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="../hardware/img/permaproto-dark.png">
  <img alt="Plan de câblage sur une carte Perma-Proto de 15 rangées, chaque rangée portant le nom de sa broche. À gauche le MAX98357A rangées 1 à 7, le bornier haut-parleur 8 et 9, le bandeau WS2812B 10 à 12, les masses des trois boutons 13 à 15. À droite le lecteur microSD rangées 1 à 6, le nœud 3,3 V rangée 7, la prise B+ rangée 8, le pont diviseur rangées 11 à 13. Le condensateur de 470 µF enjambe les deux rails centraux en rangée 9. Les straps de masse venus de gauche franchissent le rail 5 V par un pont, sans contact." src="../hardware/img/permaproto-light.png">
</picture>

La carte porte l'ampli, le condensateur, le lecteur microSD et le pont de
mesure. L'ESP32 DevKitC demanderait 19 rangées par côté contre 15 disponibles :
il se relie par fils, comme le PN532.

Le brochage tient dans deux connecteurs sertis de dix voies : positions 6 à 15
à gauche, 2 à 11 à droite. `3V3` et `5 V`, aux extrémités du header gauche,
restent isolés. D'où le choix de `35` plutôt que `34` pour la mesure de
batterie, et de `17` plutôt que `16` pour le reset du lecteur : sinon onze voies
seraient nécessaires.

## Régénérer les images du plan

```bash
cd hardware
npm ci && npx playwright install chromium
node wiring_render.mjs
```

Inutile en temps normal, l'action s'en charge. Sur une distribution sans build
Playwright de Chromium, `WIRING_CHROME=chrome` prend celui du système.

## Bandeau WS2812B

GPIO 13, 10 LED (`LED_COUNT` dans `firmware/include/Config.h`).

| Moment | Rendu |
|---|---|
| Démarrage | balayage bleu-vert |
| Mode config | bleu fixe |
| Tag reconnu | flash vert |
| Tag inconnu | double flash rouge |
| Volume ± | jauge orange, 1,6 s |
| Choix d'histoire | une zone par bouton, dans sa couleur |
| Lecture | vumètre |

Le tag inconnu est signalé par un flash et non par un son : sans mélangeur
audio, le son d'erreur est abandonné si un morceau est déjà en cours.

Chaque bouton porte une couleur (`BTN_COLOUR` dans `Config.h`) : rouge, vert,
bleu. Pendant une histoire, seules les zones menant à un nœud sont allumées, ce
qui permet à la narration de désigner un bouton par sa couleur.

**Vumètre** : un décorateur `LevelTap` mesure la crête de chaque échantillon
avant l'I2S. Affichage depuis le centre, attaque immédiate, retombée lente,
repère de crête. La mesure est prise avant le gain de volume : l'animation
reflète le contenu du son, pas le réglage. La teinte est dérivée par hachage de
l'UID du tag.

**Consommation** : jusqu'à 60 mA par LED en blanc plein. Luminosité plafonnée à
`LED_BRIGHTNESS` (30/255), limite de courant `LED_MAX_MA` appliquée par
FastLED. Poste le plus lourd après la radio : ne pas relever sans mesurer.

## Mesure de la batterie

Deux résistances de 100 kΩ entre `B+` et la masse, point milieu sur GPIO 35,
100 nF de ce point à la masse.

Le condensateur est nécessaire : l'ADC échantillonne sur un condensateur interne
à recharger en quelques microsecondes, et 50 kΩ de source n'y suffisent pas.
Sans lui la lecture chute de plusieurs centaines de millivolts.

GPIO 35 est sur ADC1 — ADC2 cesse de répondre dès que la radio tourne, ce qui
élimine les broches du header droit — et en entrée seule, suffisant pour un pont
diviseur.

La charge est lue sur la courbe de décharge, pas sur une droite : une cellule
lithium reste entre 3,9 et 3,6 V pendant l'essentiel de sa vie.

Le pont tire 21 µA en permanence, soit 15 mAh par mois. Sous 3,4 V le journal
série prévient une fois par minute et le tableau de bord affiche la tension en
rouge. Sous 3,3 V tenus 20 s, la boîte se met en veille (voir
[console.md](console.md#veille)).

Deux résistances à 1 % donnent jusqu'à 2 % d'écart sur le rapport :
`batt cal <mV>` recale la lecture sur un multimètre. La correction vit en NVS.

## Veille

En deep sleep, l'ESP32 ne tire que quelques µA, mais le reste du montage
reste sous tension :

- le module 134N3P maintient le rail 5 V ;
- les WS2812B consomment même éteintes, de l'ordre du mA par LED selon le lot ;
- le MAX98357A se met en arrêt de lui-même faute d'horloge I2S.

Avant de s'endormir, le firmware tient bas RESET du PN532 (GPIO 4, coupure
franche du lecteur) et la ligne de données du bandeau (GPIO 13, sinon flottante
et capable d'allumer des pixels au hasard). La veille réduit donc la
consommation sans l'annuler : mesurer le courant de la cellule boîte endormie
pour savoir combien de temps elle tient. Couper vraiment demanderait un
transistor sur le rail 5 V commandé par l'ESP32.
