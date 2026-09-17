# Doctype for Godot

HTML/CSS arayüzleri Godot 4'te **GPU'da** render eden bir GDExtension. Layout'u
[litehtml](https://github.com/litehtml/litehtml) yapıyor, çizimin tamamını Godot'un
2D canvas'ı üstleniyor, arada CPU rasterizer yok.
[doctype-unity](https://github.com/DilaverSerif/doctype-unity) projesinin portudur;
native katman (litehtml + quad kaydedici) aynen paylaşılır.

Ultralight/CEF gibi çözümlerden farkı: gömülü bir tarayıcı yok. litehtml saf C++
ve **hiçbir şey çizmiyor**; `document_container` arayüzü üzerinden "şu dikdörtgeni
şu renkle doldur", "şu glyph'i şuraya koy" gibi çağrılar gönderiyor. Bu sistem o
çağrıları düz bir quad akışına çevirip tek bir mesh olarak GPU'ya veriyor.

---

## Mimari

```
HtmlView.load_html(str)  ──► document::createFromString()      [C++]
        .layout          ──► document::render(max_width)
        .record          ──► document::draw()
                                     │
                                lhu::Container : document_container
                                  • create_font/text_width → stb_truetype + R8 atlas
                                  • draw_*  → LhuQuad[] (düz POD dizi)
                                     │
        ◄──── LhuQuad* + atlas pointer'ları (kopyasız) ─────────┘
        │
   DoctypeSurface → tek ArrayMesh (24 bayt/vertex) + RGBA32F veri dokusu
        │           (quad başına 8 texel: rect, yarıçaplar, kenarlık, kırpma, parametreler)
   SubViewport    → canvas_item shader, dirty rect'e kırpılmış, hedef hiç temizlenmiyor
        │
   HtmlView       → yüzeyi premultiplied alfa ile sahneye kompozit eder, girdiyi iletir
```

### Neden tek draw call

Yuvarlak köşe, kenarlık halkası, gradient ve kırpma **CPU'da rasterize
edilmiyor**. Her biri shader'da analitik olarak (signed distance field ile)
yeniden kuruluyor. Godot'ta bir 2D mesh yalnız iki özel vertex kanalı taşıdığı
için quad parametreleri bir veri dokusunda tutulur; vertex shader `CUSTOM0.x`
ile gelen quad numarasından sekiz texel okuyup fragment'e iletir:

| Quad tipi | Shader'da nasıl çiziliyor |
|---|---|
| `Rect` | Köşe başına eliptik yarıçaplı yuvarlak dikdörtgen SDF |
| `Border` | Dış SDF ∩ iç SDF (halka) + miter kama maskesi |
| `Glyph` | R8 atlastan örnekleme, vertex rengiyle tint |
| `Image` | RGBA atlastan örnekleme, yuvarlak köşe maskesi |
| `Linear/Radial/Conic gradient` | 256 px'lik LUT dokusundan, `t` fragment'te hesaplanıyor |

### Kısmi yeniden çizim

Native taraf her kaydedilen kareyi bir öncekiyle bayt bayt karşılaştırıp bir
dirty rect bildirir. Yüzey `CLEAR_MODE_NEVER` ile çalışan bir `SubViewport`
olduğundan önceki kare hedefte durur: kök canvas item dirty rect'e kırpılır,
blend'i kapalı bir quad o bölgeyi arka plan rengine boyar, mesh kırpma altında
çizilir ve viewport bir kez güncellenir. Değişmeyen sayfa **hiçbir iş yapmaz**:
kayıt yok, yükleme yok, çizim yok. Kaydırma, 2D canvas'ta texel kopyası
olmadığı için kaydırılan pencerenin tamamını yeniden boyar; dışı dokunulmaz.

---

## Kurulum

[Releases](https://github.com/DilaverSerif/doctype-godot/releases) sayfasından
macOS framework'lerini indirip `addons/doctype/bin/` altına açın ya da kök
dizindeki `SConstruct` ile bir kez derleyin (kök README); macOS derlemesi
universal (arm64 + x86_64) bir framework üretir. Sonra `addons/doctype/`
klasörünü projenize kopyalayın. Godot 4.7+.

Editörde **HtmlView** düğümü ekleyin ya da kodla:

```gdscript
var view := HtmlView.new()
add_child(view)
view.load_html("<body style='font-family:sans-serif'><h1>Merhaba</h1></body>")

view.set_text("#score", "1280")            # parse yok, artımlı
view.set_style("#bar", "width:64%")        # parse yok, artımlı
```

Üç giriş noktasının kontratı — yanlış olanı kullanmak, ölçülen performansı
kaybetmenin en kolay yolu:

| Çağrı | Ne için | Maliyet |
|---|---|---|
| `load_html` | Yapısal/doküman değişiklikleri: yeni ekran, farklı markup | Tam parse + layout. Asla kare başına çağırmayın. |
| `set_text` | Çalışma zamanı metin mutasyonu: skor, sayaç, isim | Artımlı; ~0.1 ms + kısmi redraw |
| `set_style` | Çalışma zamanı görsel/layout mutasyonu: renk, konum, bar genişliği | Artımlı; ~0.1 ms + kısmi redraw |

### Özellikler (Inspector)

| Grup | Özellik | Anlamı |
|---|---|---|
| Content | `html`, `user_css`, `html_path` | Inline markup, ek CSS, ya da bir `res://` dosyası |
| Surface | `background` | Çizimden önce yüzeyin temizlendiği renk. **Premultiplied**: şeffaf sayfa için `(0,0,0,0)`, alfası sıfır beyaz değil |
| | `match_control_size` | Yüzey Control'ün rect'ini izler (varsayılan). Kapalıysa `surface_size` kullanılır |
| | `match_content_scale` | Pencerenin/canvas'ın ölçeğini `device_scale` olarak alır: `canvas_items` stretch veya Retina'da metin keskin kalır, CSS viewport'u Control'ün canvas birimi kalır |
| | `device_scale` | `match_content_scale` kapalıyken devicePixelRatio |
| | `render_scale` | Çözünürlük çarpanı (0.25–2); layout değişmez, yalnız piksel sayısı |
| | `auto_height` | Control'ün minimum yüksekliği belge yüksekliğine ayarlanır |
| | `draw_surface` | Kapatırsanız yüzey yalnız `get_texture()` ile kullanılır (3B'de bir monitör, bir materyal) |
| Fonts | `fonts` | `FontFile` listesi; aile adı ve ağırlık dosyadan okunur, ilk aile `default_font_family` altına da kaydedilir |
| | `use_system_fonts` | Liste boşsa platformun Arial/Helvetica/Roboto vb. fontu |
| Document | `master_stylesheet` | `Game UI` (kırpılmış, ~2.4x hızlı doküman) / `Full` |
| | `language`, `culture` | `tr` / `tr-TR`: text-transform'da dört yönlü i/İ/ı/I eşlemesi |
| Input | `pass_through_empty_areas` | Yalnız `id` taşıyan elemanlar dokunuşu yakalar; boş sayfa arkadaki oyuna geçer |
| | `drag_to_scroll` | Parmakla/fare ile sürükleme overflow:auto kutuları kaydırır |
| | `drag_items` | `id`'li bir elemandan Godot drag-and-drop başlatır |

### Sinyaller

| Sinyal | Ne zaman |
|---|---|
| `anchor_clicked(url)` | `<a href>` tıklandı (gamepad ile `activate` dahil) |
| `element_clicked(id, tag, class_names, action)` | Diğer elemanlar; en içteki önce, `bind_click` bağı olmayanlar yukarı kabarır |
| `cursor_changed(cursor)` | CSS `cursor` değişti (Control'ün imleci de otomatik ayarlanır) |
| `document_loaded` | `load_html` sonrası, `bind_click` için iyi bir yer |
| `item_drag_started(element_id, document_point)` | `drag_items` açıkken sürükleme başladı |
| `item_dropped(data, target_id, document_point)` | Bu yüzeye bir Doctype öğesi bırakıldı |

```gdscript
view.bind_click("save", func(click): print("Kaydet: ", click["action"]))
# <span id="save" data-action="save-game">Kaydet</span>
```

### Girdi modeli

`HtmlView` bir Control'dür; `_gui_input` ile fare hareketi (`:hover`), sol tuş
(`:active`, tıklama), tekerlek/trackpad (kaydırma) ve dokunmatik olayları belge
koordinatlarına çevirip iletir. Bir kaydırma sayfadaki hiçbir kutu tarafından
tüketilmezse olay kabul edilmez ve üstteki `ScrollContainer`'a düşer.

`element_at(point)` saf bir sorgudur: hover durumunu değiştirmez, sürükleme
sırasında bırakma hedefini yoklamak için güvenlidir. `get_element_rect(sel)`
son layout'taki kutuyu CSS piksel olarak verir; `to_document_point` /
`from_document_point` Control yerel koordinatlarıyla belge arasında çevirir.

#### Ekranı kaplayan ama doldurmayan HUD'lar

`pass_through_empty_areas` açıkken `_has_point` yalnız `id`'li elemanların
üstünde `true` döner; boş sayfaya düşen dokunuş arkadaki oyuna geçer.
Dekoratif sarmalayıcılara `id` vermeyin, tıklanabilir kutulara verin.
Şeffaf sayfa için `background` **(0,0,0,0)** olmalı.

#### Birden fazla panel arasında sürükleme

`drag_items` açık iki `HtmlView` arasında Godot'un kendi drag-and-drop'u
çalışır: `_get_drag_data` parmağın altındaki `id`'li elemanı bir Dictionary
(`view`, `element_id`, `document_point`) olarak verir, hedef yüzey
`item_dropped` sinyalini yayar. Sürüklenen ikonu göstermek için
`item_drag_started` içinde `set_drag_preview` çağırın.

#### Gamepad / klavye ile gezinme

Focus, pointer emülasyonu değil, kendi durumudur. Bir eleman gezinmeye yalnız
`tabindex` taşıyorsa katılır (değeri "-1" hariç):

```html
<div id="slot1" tabindex="0" data-nav-up="helmet">...</div>
```
```css
#slot1:focus { border-color:#3b82f6; }
```

Control odak alınca (klavye/gamepad ile) sayfa hatırladığı elemana, yoksa sol
üstteki odaklanabilire girer; `ui_up/right/down/left` sayfa içinde
`move_focus` çalıştırır, sayfada gidecek yer kalmayınca olay kabul edilmez ve
Godot odağı `focus_neighbor_*` ile komşu Control'e devreder. `ui_accept`
odaklı elemanı gerçek tıklama yolundan etkinleştirir. Programatik olarak
`set_document_focus("#slot1")`, `move_focus(HtmlView.NAV_RIGHT)`,
`activate()`, `get_focused_id()`.

### Görseller

`<img src="res://icons/gold.png">` doğrudan `ResourceLoader` ile yüklenir
(şema yoksa `res://` denenir). Diskte olmayan bir dokuyu adla kaydedin:

```gdscript
view.register_image("gold", my_texture)   # <img src="gold">
```

Tüm görseller tek bir atlasa paketlenir (`Geometry2D.make_atlas`); atlas
`max_atlas_size`'a sığmazsa görseller bulanıklaştırılmak yerine hata verilir.
Yeni bir görsel geldiğinde layout yeniden koşar ve native'in retained display
list'i düşürülür (UV'ler taşınmış olabilir).

### Fontlar

litehtml font yüklemez; siz vermelisiniz:

1. **Önerilen:** `.ttf`/`.otf` dosyalarını projeye koyun (Godot dinamik font
   olarak içe aktarır) ve `fonts` listesine ekleyin. Aile adı, ağırlık ve
   italik dosyanın kendi bilgisinden okunur; CSS'te o adla kullanın. İlk
   ailenin bütün yüzleri (kalın, italik) `default_font_family` altına da
   kaydedilir, yani `font-family:sans-serif` projenin fontunu bulur.
2. **Hızlı başlangıç:** `use_system_fonts` açık kalsın; macOS'ta Arial,
   Android'de Roboto, Windows'ta Arial/Segoe UI bulunur. Layout cihazdan cihaza
   değişebileceği için üretimde kendi fontunuzu gömün.

### Yüzeyi başka yerde kullanmak

`get_texture()` `SubViewport`'un dokusunu verir; bir `Sprite3D`'ye, bir
`StandardMaterial3D`'nin albedo'suna ya da başka bir `TextureRect`'e
verilebilir. Yüzey premultiplied alfa taşır: 2D'de `CanvasItemMaterial`
`BLEND_MODE_PREMULT_ALPHA`, 3B'de `transparency` + `blend_mode = premul alpha`
kullanın. Sayfayı yalnız doku olarak kullanıyorsanız `draw_surface`'ı kapatın.

---

## Testler

Kök README'de: native harness (188 check, Godot'suz), gerçek GPU ile
`test/*.gd` betikleri (render, kısmi yeniden çizim, girdi yönlendirme, focus,
kaydırma, içerik ölçeği, Türkçe text-transform), üç renderer'da da.

## Kapsam

**Çalışan:** blok/inline/float layout, flexbox, tablolar, `border-radius`
(eliptik dahil), kenar başına kenarlık, linear/radial/conic gradient, `overflow`
kırpma, metin dekorasyonları, liste işaretleyicileri, `:hover`/`:active`/`:focus`,
`<a>` tıklaması, `@media` sorguları, `<link>`/`@import` (`res://` yolundan).

**Yok:**
- **JavaScript.** Dinamik UI için DOM'u GDScript'ten `set_text`/`set_style`
  ile güncelleyin.
- **CSS animasyon/transition/transform.** litehtml desteklemiyor; demo
  sayfasındaki animasyonlar `_process` içinde `set_style` ile yazılıyor.
- **Karmaşık metin şekillendirme.** stb_truetype: Latin/Türkçe/Kiril için
  yeterli; Arapça/Farsça/Hintçe için HarfBuzz + BiDi gerekir.
- `border-style` yalnızca `solid`.

## Lisanslar

| Bileşen | Lisans |
|---|---|
| litehtml | BSD-3-Clause |
| gumbo-parser (litehtml içinde) | Apache-2.0 |
| stb_truetype | Public domain / MIT |
| godot-cpp | MIT |
| Doctype | MIT |
