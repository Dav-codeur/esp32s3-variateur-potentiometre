# Variateur à potentiomètre sur ESP32-S3

Projet réalisé avec ESP-IDF sur une carte ESP32-S3. Un potentiomètre règle l'intensité d'une sortie PWM, un bouton change le type de réponse, et la tension mesurée est envoyée sur le port série.

## Principe

Le potentiomètre forme un diviseur de tension dont le point milieu est relié à une entrée de l'ADC. Un timer déclenche une lecture toutes les 20 ms, sans aucun délai bloquant dans le programme principal. La valeur brute est convertie en millivolts grâce à la calibration d'usine de l'ESP32-S3 (curve fitting), car l'ADC n'est pas parfaitement linéaire.

La tension mesurée fixe le rapport cyclique d'un signal PWM (périphérique LEDC, 5 kHz, résolution 12 bits).

Un bouton, géré par interruption avec anti-rebond par timer (50 ms), bascule entre deux modes de réponse :

- **linéaire** : le rapport cyclique est proportionnel à la tension ;
- **corrigé** : le rapport cyclique est élevé au carré, ce qui compense la perception non linéaire de la luminosité par l'oeil.

Une zone morte de 50 mV garantit que la sortie est bien à zéro lorsque le potentiomètre est au minimum.

La tension mesurée et le mode actif sont affichés sur le port série une fois par seconde.

## Matériel

- Carte ESP32-S3
- Potentiomètre
- LED et résistance de 220 à 330 ohms
- Bouton poussoir

## Câblage

| Élément | Broche |
|---|---|
| Potentiomètre (curseur) | GPIO5 (ADC1, canal 4) |
| Sortie PWM vers la LED | GPIO18 |
| Bouton | GPIO4 (l'autre patte sur GND, pull-up interne) |

Les extrémités du potentiomètre vont sur 3V3 et GND. La LED est montée en série avec sa résistance entre GPIO18 et GND.

## Compilation et flash

```
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash monitor
```

Remplacer `COMx` par le port de la carte.

## Résultat attendu

La LED réagit sans à-coups à la rotation du potentiomètre, et un appui sur le bouton ne provoque jamais deux changements de mode. Sur le port série, on lit par exemple :

```
mode=lineaire  raw=2048  tension=1650 mV
```

## Limites connues

Avec l'atténuation de 12 dB, l'ADC sature autour de 3,1 V : la tension maximale lue est donc inférieure aux 3,3 V réellement présents sur la broche. La constante `VMAX_MV` du code en tient compte.
