# Catture pre-2026-09-08 sera

Archiviate quando il firmware è passato a v4. Sono ancora valide come dati, ma
**il loro marker `go` non lo è**: fino a v3 era una pressione di tasto umana,
quindi ±100 ms. Su `accel_20260907_160108.csv` cade ~90 ms *dopo* il movimento
che dovrebbe marcare.

Usale per validare il *meccanismo* (rate, integrità, posizionamento a livello di
campione). **Non usarle per tarare le soglie del detector**: tarare una soglia da
±10 ms contro un riferimento da ±100 ms è circolare. Per quello servono catture
v4, dove il `go` lo genera il firmware.

Altre differenze rispetto al formato v4:

- niente pre-roll: la registrazione partiva esattamente al `set`, quindi il
  detector è cieco per i primi ~800 ms (il tempo di convergenza dell'LTA) —
  proprio dentro la finestra in cui una falsa partenza conta
- rate ~977 Hz (v2, free-running) o ~863 Hz (v3); il detector lo ricava dai
  timestamp, quindi si aprono lo stesso
- i marker sono in fondo al file, non in testa
- colonna `host_iso` in più, ignorata

`blockstart_20260831_test0.csv` e i due `kinestart_imu_*` sono più vecchi ancora
e usano formati di colonne diversi.
