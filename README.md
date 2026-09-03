# Papemaru C Chess

C言語で作っている、小さなチェスエンジンです。

チェスの盤面を読み、ルールに従って指せる手を調べ、その中から次に指す手を考えます。

まだシンプルなエンジンですが、チェスの基本的なルール、合法手の生成、局面の評価、先の手を読む探索などを自分で実装しています。今後、少しずつ探索や評価の仕組みを改善して、どこまで強くできるかを試していくためのプロジェクトです。

## 現在の状態

現在のPapemaru C Chessは、チェスエンジンとして動作するための基本的な機能を持っています。

主な機能は次のとおりです。

- チェス盤と駒の状態を扱える（Bitboardベース）
- 合法な手を生成できる
- チェックを判定できる
- キャスリングに対応している
- アンパッサンに対応している
- FENから局面を読み込める
- UCI形式のコマンドを受け取れる
- Alpha-Beta pruningを使って手を読む
- Negamaxを使って探索する
- Quiescence searchを使って、駒の取り合いが続いている局面をもう少し深く読む
- Perftを使って合法手生成をテストできる（perft divideにも対応）

一方で、まだ強いチェスエンジンとは言えません。

現在の評価は基本的に「盤上にどの駒が残っているか」を中心にしています。そのため、駒得以外の細かな局面の良し悪しを十分に判断できません。

また、探索を効率よくするための高度な仕組みも、まだ多く入っていません。

このシンプルな状態から少しずつ改良していくことを、このプロジェクトの一つの目的にしています。

## 盤面表現（Bitboard）

Papemaru C Chessの盤面は、`int sq[64]` のようなマス目配列（mailbox）ではなく、`uint64_t` を使った **Bitboard** を中心に表現しています。

```c
typedef uint64_t Bitboard;

typedef struct {
    Bitboard pieces[2][6];
    Bitboard occupied[2];
    Bitboard all;

    int side;
    int enpas;
    int castle;
} Board;
```

- `pieces[color][type]` — 白/黒それぞれのポーン、ナイト、ビショップ、ルーク、クイーン、キングの位置をBitboardとして保持します。
- `occupied[WHITE]` / `occupied[BLACK]` / `all` — 各色の駒がある場所、全ての駒がある場所を表すBitboardです。`make_move`/`unmake_move`のたびに`pieces`から再計算され、常に一貫した状態が保たれます。

盤面のマス番号（square index）は、以前のmailbox実装から変更していません。

```text
a1 = 0, b1 = 1, ..., h1 = 7
a2 = 8, ...
a8 = 56, ..., h8 = 63
```

そのため、FEN・UCIのmove文字列（`e2e4`など）・PST（Piece-Square Table）の向きは、以前と同じ意味で扱われます。

### Attack table / Sliding piece attacks

ナイト・キング・ポーンの利き（attack）は、起動時に `bitboard_init_tables()` で64マス分のテーブルとして事前計算しています（`knight_attacks[64]`, `king_attacks[64]`, `pawn_attacks[2][64]`）。

ビショップ・ルーク・クイーンの利きは、Magic Bitboardsのような高速な方式ではなく、盤上の駒（occupancy）を見ながら4方向にレイを伸ばして計算する、シンプルで分かりやすい方式（`bishop_attacks()` / `rook_attacks()`）を採用しています。

`is_attacked()` / `is_in_check()` も、64マスを走査する代わりに、これらのBitboardとattack tableのAND演算だけで判定できるようにしています。

### 手生成（Move generation）とmake/unmake

手生成は、各駒の種類ごとにBitboardを走査して行います（例：ナイトなら `pieces[us][KNIGHT]` を1駒ずつ取り出し、`knight_attacks[from] & ~occupied[us]` で移動先を求める、という流れです）。ポーンについては、push・二歩・斜め取り・アンパッサン・プロモーションをBitboardのシフト演算でまとめて生成しています。

局面の適用・巻き戻しは、以前の「盤面を丸ごとコピー・復元する」方式から、`make_move()` / `unmake_move()` による差分適用方式に変更しました。

```c
Undo u;
if (make_move(&board, move, &u)) {
    /* ここで探索など */
    unmake_move(&board, move, &u);
}
```

`make_move()` は指し手が自分のキングを取られる状態にしないかを確認し、合法であれば盤面を更新して`1`を返します。非合法であれば、内部で変更を巻き戻したうえで`0`を返します（呼び出し側での盤面コピーは不要です）。

## 探索の仕組み

Papemaru C Chessは、いくつか先の手を読んで、最も良さそうな手を選びます。

探索には主にNegamaxとAlpha-Beta pruningを使っています。

Alpha-Beta pruningは、すでに悪いと分かっている手を最後まで調べずに省略することで、読む手の数を減らす仕組みです。

また、通常の探索が終わった後も、駒を取る手などをもう少し読むQuiescence searchを使っています。

現在はまだ探索の仕組みがシンプルなので、同じ局面を何度も調べることがあります。今後はこの部分を改善して、より深く読めるエンジンにしていく予定です。

## 局面の評価

現在の評価は、主に駒の価値を使っています。

| 駒 | 価値 |
|---|---:|
| ポーン | 100 |
| ナイト | 320 |
| ビショップ | 330 |
| ルーク | 500 |
| クイーン | 900 |
| キング | 20000 |

例えば、自分のクイーンが相手のクイーンより多く残っていれば、自分にとって良い局面として評価します。

ただし、実際のチェスでは駒の数だけで局面の良し悪しは決まりません。

「キングが安全か」「駒がよく働いているか」「ポーンの形はどうか」といった情報も重要です。

そこで、駒得とPST（Piece-Square Table）に加えて、`eval.c` / `eval.h` に次の8つの評価項目を追加しました（追加した順）。

1. **Passed Pawn（パスポーン）** — 自ファイルと隣接ファイルの前方に敵ポーンがいないポーンにボーナス。ゴールに近いほどボーナスが大きくなります。
2. **Mobility（駒の可動域）** — ナイト・ビショップ・ルーク・クイーンについて、動ける（自駒がない）マスの数に応じてボーナス。
3. **Pawn Shield（ポーンシールド）** — キングがサイド（a〜cファイル、f〜hファイル）にいるとき、その前方2ランドにある自ポーンの枚数に応じてボーナス。
4. **Isolated Pawn（孤立ポーン）** — 隣接ファイルに自分のポーンが1枚もないポーンにペナルティ。
5. **Doubled Pawn（ダブルポーン）** — 同じファイルに2枚以上ポーンがあるとき、2枚目以降にペナルティ。
6. **Bishop Pair（ビショップペア）** — ビショップを2枚とも保持しているサイドにボーナス。
7. **Rook Open File（ルークのオープンファイル）** — ルークがいるファイルに自分のポーンがない場合にボーナス（相手のポーンも無ければさらに大きいボーナス）。
8. **Knight Outpost（ナイトのアウトポスト）** — 敵陣寄りのランクにいて、自分のポーンに守られていて、かつ将来的にも敵ポーンに追い払われないマスにいるナイトにボーナス。

これらはすべて`eval_extra()`（`eval.c`）としてまとめられ、`search.c`の`evaluate()`から呼び出されます。今後もこの評価関数は少しずつ拡張・調整していく予定です。

## UCI

Papemaru C ChessはUCI（Universal Chess Interface）形式のコマンドを扱います。

UCIは、チェスエンジンとチェスGUIなどのプログラムを接続するための一般的な通信方式です。

そのため、UCIに対応したチェスGUIからエンジンとして利用できる可能性があります。

現在の実装では、例えば次のようなコマンドを扱えます。

```text
uci
isready
ucinewgame
position startpos
position fen ...
go depth 4
go movetime 1000
stop
quit
```

また、デバッグ用に `d` で現在の盤面を表示できます。

## ビルド

GCCとmakeが使える環境でビルドできます。

```bash
make
```

MakefileではC17を指定しています。

```text
-std=c17
```

また、コンパイル時には次の警告オプションを使用しています。

```text
-Wall -Wextra
```

ビルドすると、現在の設定では次の実行ファイルが作られます。

```text
PapemaruCChess_v20260829
```

## 実行

ビルド後、次のように起動できます。

```bash
./PapemaruCChess_v20260829
```

起動するとUCIコマンドを入力できます。

例えば、

```text
uci
isready
position startpos
go depth 4
```

と入力すると、開始局面から4手分の探索を行い、最後に `bestmove` を返します。

## Perft

Perftは、チェスのルール上指すことができる手を、指定した深さまで全部調べて数えるテストです。

チェスエンジンでは、合法手生成が正しく動いているかを確認するためによく使われます。

開始局面では、次のような結果になります。

```text
depth 1: 20
depth 2: 400
depth 3: 8902
depth 4: 197281
```

UCIからは、例えば次のように実行できます。

```text
go perft 4
```

または、

```text
perft 4
```

と入力できます。

### Perft divide

Bitboard移行時のデバッグに使いやすいよう、`perft divide`相当の機能も追加しました。ルートの指し手ごとに、その先のノード数を表示します。

```text
divide 2
```

```text
a2a3: 20
b2b3: 20
...
b1c3: 20
g1h3: 20
info string perft divide 2 total = 400
```

`go perft divide 4` のように `go` を前置しても、`perft divide 4` のように単独でも実行できます。

## ファイル構成

現在の主なファイルは次のとおりです。

```text
.
├── main.c
├── uci.c
├── uci.h
├── board.c
├── board.h
├── bitboard.c
├── bitboard.h
├── movegen.c
├── eval.c
├── eval.h
├── search.c
├── search.h
├── perft.c
├── perft.h
└── Makefile
```

それぞれの役割は次のとおりです。

- `main.c` — プログラムの開始部分
- `uci.c / uci.h` — UCIコマンドの処理
- `board.c / board.h` — 盤面（Bitboard）の状態管理、FEN読み込み、make/unmake、盤面表示
- `bitboard.c / bitboard.h` — Bitboardのユーティリティ、attack table、sliding piece attacks
- `movegen.c` — Bitboardベースの合法手生成、`is_attacked` / `is_in_check`
- `eval.c / eval.h` — 局面評価の追加項目（Passed Pawn、Mobility、Pawn Shield、Isolated/Doubled Pawn、Bishop Pair、Rook Open File、Knight Outpost）
- `search.c / search.h` — 局面評価（駒得・PST・`eval_extra()`の呼び出し）と探索
- `perft.c / perft.h` — 合法手生成のテスト（perft divideを含む）
- `Makefile` — ビルド方法の定義

## これから

このエンジンは、いきなり大きなチェスエンジンを作るのではなく、動くものを少しずつ強くしていく方針です。

Bitboardへの移行が完了したので、次の段階では、まず「もっと深く、効率よく読む」ことを目標にしています。

候補として、次のような改良を考えています。

- Zobrist Hash
- Transposition Table
- Move Ordering（Killer Move / History Heuristic）
- PVS（Principal Variation Search）
- Null Move Pruning
- Late Move Reduction
- Quiescence Searchの改善（SEE、Delta Pruningなど）

その先には、評価関数の改善や、さらに高度な探索手法なども検討できます。

ただし、どこまで強くできるかは実際に実装して試しながら進めていきます。

## 開発について

Papemaru C Chessは、小さなチェスエンジンを自分で作りながら、チェスのルール、合法手生成、探索アルゴリズム、評価関数などを学び、実験するためのプロジェクトです。

コードを大きく書き換えて一気に完成させるのではなく、一つずつ仕組みを追加して、変更する前と後で強さや動作を比較していきます。

## ライセンス

このリポジトリにライセンスを適用する場合は、別途LICENSEファイルを用意してください。

現時点では、特定のオープンソースライセンスをこのREADMEから指定していません。
