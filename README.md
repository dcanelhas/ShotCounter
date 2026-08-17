# Shot & Recoil Counter
For Pebble Time 2

## Description
This app uses the accelerometer to measure recoil (as spike in acceleration magnitude) in order to count the number of shots taken in sports shooting.

## Settings:
- Color Theme (default: Dark Mono)
- Acceleration threshold, set this to adjust for different calibers or other physical activities
- Maximum rate of fire, set this to suppresses events happening after an initial detection to avoid false positives. (default: 3 Rounds Per Second)
- Show Mags, toggle to display how many magazines have been spent (default: on)
- Show Debug Energy Overlay

## Usage instructions:
- Short press up or down to change the sensitivity threshold
- Long press up or down to change mag capacity
- Short press select to reset count
- Long press select to cycle through color themes

LICENSE covers the use of everything in this repository except for the font,
which is distributed under the SIL OpenFont License (resources/fonts/LICENSE) 

## Releases
New releases will be made available on the [Pebble AppStore](https://apps.repebble.com/d9ae0d91c6b94bd2add22405)

## Note for Developers:

If you want to build/modify this you can import the project directly from the github URL as a new project to cloudpebble.repebble.com
You can then go to Build & Run to Run Build, Download the pbw file and open it with the Pebble app to run it on your pebble watch
