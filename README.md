# PapemaruCChess

Linux向けC製チェスエンジン。UCIをフロントエンドとし、反復深化・TT・各種枝刈り・時間管理を実装しています。

## UCI

基本コマンド:

- `uci`
- `isready`
- `ucinewgame`
- `position startpos [moves ...]`
- `position fen <FEN> [moves ...]`
- `go depth N`
- `go movetime MS`
- `go wtime MS btime MS winc MS binc MS [movestogo N]`
- `go nodes N`
- `go infinite`
- `go ponder ...`
- `go ... searchmoves e2e4 d2d4 ...`
- `stop`
- `ponderhit`
- `quit`

診断用として `help` / `ucihelp` / `d` / `perft` / `divide` も利用できます。

## UCI options

- `Hash`: transposition table size in MB (1..1024)
- `Clear Hash`: transposition table clear button
- `Move Overhead`: GUI/OS/通信遅延を吸収するための予約時間 (ms)
- `Slow Mover`: 時間配分の倍率。100が基準
- `Ponder`: ponder searchの有効/無効

## TimeManager v3

通常時間制御では、残り時間・Increment・MovesToGo・Move Overhead・Slow Moverからsoft/hard budgetを計算します。

hard deadlineは時計切れを避けるために残り時間の一部を必ず残します。soft deadlineは通常の停止点で、hard deadlineは最後の安全弁です。

さらに、反復深化ごとに以下を統計として蓄積し、`info string timemgr ...` 行で毎iteration公開します。

- **root bestmoveの変更回数・安定性**: bestmoveが変わった回数 (`changes`) と、直近で連続して変わっていないiteration数 (`stability`)。
- **score volatility**: 単純な固定しきい値ではなく、直前iterationとの評価値差分のEWMA (`volatility_ewma`) と、Welfordのオンラインアルゴリズムによる標準偏差 (`volatility_sd`) を継続的に更新し、これらを時間延長の圧力計算に連続的に反映します。
- **深さごとの探索時間予測**: 各深さの実測所要時間を記録し、直近2深さの比から実効分岐係数 (EBF, `ebf`) を指数平滑で推定します。
- **次のiterationに必要な時間の予測**: 直前深さの所要時間 × EBF × (aspiration再探索オーバーヘッド補正) で次のiterationのコストを予測し (`predicted_next_ms`)、残りのhard budgetに対して安全マージン付きで収まらないと判断した場合はそのiterationを開始しません（開始しても中断されれば結果は破棄され、費やした時間が無駄になるため）。
- **Mate / tactical positionでの強制延長**: ルート局面がcheck中・合法手が少ない・capture比率が高い場合を「tactical」と判定し、soft/hard の上限キャップを引き上げます。Mateスコアを検出した場合はさらに上限を引き上げ、直前iterationと詰み手数が変化した（＝まだ手順が安定していない）場合は強い圧力を加えて時間を延長し、安定するまで探索を継続させます。
- **fail-low / fail-high時の時間延長**: aspiration windowの再探索が発生した瞬間に即座にsoft/hardを一時的に押し上げ、そのiterationの終了時にも累積したfail-high/fail-low回数を圧力として反映します。
- **aspiration windowの再探索コストの通知**: fail-low/high一回ごとにそのpass（全root手の再探索）に要した実測時間をTimeManagerへ通知し、iteration全体に占める再探索オーバーヘッド比率を指数平滑で保持して、次のiteration予測にも反映します。

`ponder` 中は時計を消費せず（soft/hardは無制限のまま）、深さごとの所要時間履歴・EBF推定・bestmove安定性・score volatility・fail-high/low累計・mate/tactical判定は通常どおり収集され続けます。`ponderhit` を受け取った瞬間、時計の基準点だけを付け替え（進行中の深さの開始マーカーもponder時間分だけシフトして継続性を保つ）、これらの統計は一切破棄しません。これにより、ponderhit直後の最初のiteration予測から、pondering中に得た本物の探索データがそのまま活用されます。

## TT / hashfull

`tt_hashfull()` は、TTクラスタの先頭最大1000個をサンプリングし、現在の探索世代 (generation) で使用中のスロット比率を1000分率で返します。`info depth ...` 行の `hashfull` フィールドはこの実測値です。

## PV construction

PVはTTを辿って事後的に再構築するのではなく、探索自身が三角形PVテーブル (triangular PV table) をPVノードでのみ更新することで直接構築します。TTエントリは他の分岐や後続のiterationで上書きされうるため、TT依存のPVは信頼性に欠けますが、探索由来のPVは常にその時点で実際に読んだ手順と一致します。

## UCI info

各completed iterationについて、概ね次を出力します。

`info depth ... seldepth ... score ... nodes ... time ... nps ... hashfull ... pv ...`

続けて診断用の `info string timemgr ...` 行を出力し、上記の統計値（bestmove変更回数・安定性・score volatility・EBF・次iteration予測時間・fail-high/low累計・aspiration再探索コスト・tactical判定・現在のsoft/hard budget）を公開します。標準的なUCI GUIはこの行を無視して問題ありません。

Mate scoreはUCIの `score mate N` に変換します。PVは探索が直接構築した手順です。

## Build

```sh
make
```

必要環境:

- Linux / POSIX
- GCCまたはClang
- pthread
- libm

## Perft sanity check

startposで、少なくとも以下を確認しています。

- depth 1 = 20
- depth 2 = 400
- depth 3 = 8902
- depth 4 = 197281
- depth 5 = 4865609
