// 文档同步桥：把 src/class-catalog.js 的五个渲染块交给 Python 脚本。
// 单一真源仍是 class-catalog.js；这里只输出 JSON，不复制任何文案。
import { renderCatalogBlocks, CATALOG_VERSION } from '../src/class-catalog.js';
process.stdout.write(JSON.stringify({ version: CATALOG_VERSION, blocks: renderCatalogBlocks() }) + '\n');
