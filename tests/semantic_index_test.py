#!/usr/bin/env python3

import base64
import importlib.machinery
import importlib.util
import io
from pathlib import Path
import sqlite3
import sys
import tempfile
import unittest

import numpy as np


def load_worker(path: str):
    loader = importlib.machinery.SourceFileLoader("synchro_semantic_index", path)
    spec = importlib.util.spec_from_loader(loader.name, loader)
    module = importlib.util.module_from_spec(spec)
    loader.exec_module(module)
    return module


WORKER = load_worker(sys.argv[1])
del sys.argv[1]


class SemanticIndexTest(unittest.TestCase):
    def test_status_reports_current_pending_and_stale_coverage(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            catalog_path = root / "catalog.sqlite"
            database_path = root / "semantic.sqlite"
            catalog = sqlite3.connect(catalog_path)
            catalog.executescript(
                """
                CREATE TABLE files(
                  path TEXT PRIMARY KEY,file_id TEXT,extension TEXT,
                  is_dir INTEGER,is_symlink INTEGER,size INTEGER,mtime INTEGER
                );
                CREATE TABLE catalog_meta(key TEXT PRIMARY KEY,value TEXT);
                INSERT INTO catalog_meta VALUES('change_sequence','7');
                INSERT INTO files VALUES('/a/blue.jpg','a','jpg',0,0,10,20);
                INSERT INTO files VALUES('/a/new.png','b','png',0,0,11,21);
                INSERT INTO files VALUES('/a/readme.txt','c','txt',0,0,12,22);
                """
            )
            catalog.commit()
            catalog.close()

            semantic = WORKER.connect_database(database_path)
            semantic.executemany(
                "INSERT INTO image_embeddings(file_id,path,model,dimension,"
                "source_size,source_mtime,vector,updated_at) "
                "VALUES(?,?,?,?,?,?,?,?)",
                [
                    ("a", "/a/blue.jpg", WORKER.IMAGE_MODEL_ID, 512, 10, 20,
                     bytes(512 * 4), 1),
                    ("gone", "/gone.jpg", WORKER.IMAGE_MODEL_ID, 512, 1, 1,
                     bytes(512 * 4), 1),
                ],
            )
            WORKER.meta_set(semantic, "change_sequence", 5)
            semantic.commit()
            semantic.close()

            status = WORKER.semantic_status(
                catalog_path, database_path, root / "vision.onnx",
                root / "text.onnx", root / "tokenizer.json"
            )
            self.assertEqual(status["eligibleImages"], 2)
            self.assertEqual(status["currentEmbeddings"], 1)
            self.assertEqual(status["pendingImages"], 1)
            self.assertEqual(status["staleEmbeddings"], 1)
            self.assertEqual(status["coveragePercent"], 50.0)
            self.assertEqual(status["catalogSequenceLag"], 2)

    def test_exact_ranking_is_scoped_to_current_folder(self):
        with tempfile.TemporaryDirectory() as directory:
            database = Path(directory) / "semantic.sqlite"
            db = WORKER.connect_database(database)
            blue = np.zeros(512, dtype=np.float32)
            blue[0] = 1.0
            purple = np.zeros(512, dtype=np.float32)
            purple[0] = 0.7
            purple[1] = 0.7
            purple /= np.linalg.norm(purple)
            red = np.zeros(512, dtype=np.float32)
            red[1] = 1.0
            db.executemany(
                "INSERT INTO image_embeddings(file_id,path,model,dimension,"
                "source_size,source_mtime,vector,updated_at) "
                "VALUES(?,?,?,?,?,?,?,?)",
                [
                    ("1", "/scope/blue.jpg", WORKER.IMAGE_MODEL_ID, 512, 1, 1,
                     blue.tobytes(), 1),
                    ("2", "/scope/purple.jpg", WORKER.IMAGE_MODEL_ID, 512, 1, 1,
                     purple.tobytes(), 1),
                    ("3", "/scope/red.jpg", WORKER.IMAGE_MODEL_ID, 512, 1, 1,
                     red.tobytes(), 1),
                    ("4", "/elsewhere/bluer.jpg", WORKER.IMAGE_MODEL_ID, 512, 1, 1,
                     blue.tobytes(), 1),
                ],
            )
            db.commit()

            ranked, searched = WORKER.rank_image_embeddings(db, blue, "/scope", 2)
            db.close()

            self.assertEqual(searched, 3)
            self.assertEqual([path for _, path in ranked],
                             ["/scope/blue.jpg", "/scope/purple.jpg"])
            self.assertAlmostEqual(ranked[0][0], 1.0, places=6)

    def test_search_selection_rejects_weak_and_collapses_near_duplicates(self):
        primary = np.zeros(512, dtype=np.float32)
        primary[0] = 1.0
        duplicate = primary.copy()
        duplicate[1] = 0.01
        duplicate /= np.linalg.norm(duplicate)
        distinct = np.zeros(512, dtype=np.float32)
        distinct[1] = 1.0
        weak = np.zeros(512, dtype=np.float32)
        weak[2] = 1.0
        ranked = [
            (0.31, "/scope/primary.jpg", primary.tobytes()),
            (0.30, "/scope/copy.webp", duplicate.tobytes()),
            (0.24, "/scope/distinct.png", distinct.tobytes()),
            (0.19, "/scope/unrelated.png", weak.tobytes()),
        ]

        selected, rejected, collapsed = WORKER.select_confident_diverse(
            ranked, 20
        )

        self.assertEqual(
            [path for _, path in selected],
            ["/scope/primary.jpg", "/scope/distinct.png"],
        )
        self.assertEqual(rejected, 1)
        self.assertEqual(collapsed, 1)

    def test_scoped_coverage_counts_only_eligible_images_below_folder(self):
        with tempfile.TemporaryDirectory() as directory:
            catalog = sqlite3.connect(Path(directory) / "catalog.sqlite")
            catalog.executescript(
                """
                CREATE TABLE files(
                  path TEXT PRIMARY KEY,extension TEXT,is_dir INTEGER,
                  is_symlink INTEGER
                );
                INSERT INTO files VALUES('/scope/a.jpg','jpg',0,0);
                INSERT INTO files VALUES('/scope/nested/b.PNG','png',0,0);
                INSERT INTO files VALUES('/scope/readme.md','md',0,0);
                INSERT INTO files VALUES('/scope/folder','',1,0);
                INSERT INTO files VALUES('/scope/link.webp','webp',0,1);
                INSERT INTO files VALUES('/elsewhere/c.jpg','jpg',0,0);
                """
            )
            self.assertEqual(WORKER.scoped_eligible_images(catalog, "/scope"), 2)
            catalog.close()

    def test_hot_folder_queue_skips_current_and_nested_images(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            catalog_path = root / "catalog.sqlite"
            catalog = sqlite3.connect(catalog_path)
            catalog.executescript(
                """
                CREATE TABLE files(
                  path TEXT PRIMARY KEY,file_id TEXT,parent TEXT,
                  extension TEXT,is_dir INTEGER,is_symlink INTEGER,
                  size INTEGER,mtime INTEGER
                );
                CREATE INDEX files_parent ON files(parent);
                INSERT INTO files VALUES
                  ('/hot/current.jpg','current','/hot','jpg',0,0,10,20),
                  ('/hot/new.png','new','/hot','png',0,0,11,30),
                  ('/hot/nested/deep.webp','deep','/hot/nested','webp',0,0,12,40),
                  ('/other/also-new.jpg','other','/other','jpg',0,0,13,50);
                """
            )
            catalog.commit()
            catalog.close()

            semantic = WORKER.connect_database(root / "semantic.sqlite")
            semantic.execute(
                "INSERT INTO image_embeddings(file_id,path,model,dimension,"
                "source_size,source_mtime,vector,updated_at) "
                "VALUES(?,?,?,?,?,?,?,?)",
                (
                    "current", "/hot/current.jpg", WORKER.IMAGE_MODEL_ID,
                    512, 10, 20, bytes(512 * 4), 1,
                ),
            )
            semantic.execute(
                "ATTACH DATABASE ? AS catalog_live",
                (f"file:{catalog_path}?mode=ro",),
            )

            rows = WORKER.priority_candidates(semantic, [Path("/hot")], 8)
            semantic.close()

            self.assertEqual([row[2] for row in rows], ["/hot/new.png"])

    @unittest.skipUnless(
        importlib.util.find_spec("PIL"), "optional Pillow runtime is unavailable"
    )
    def test_preprocess_reuses_decode_for_compact_visual_fact_sample(self):
        from PIL import Image

        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "wide.png"
            Image.new("RGB", (240, 120), (20, 80, 160)).save(source)

            accepted, pixels, samples = WORKER.preprocess(
                [str(source)], emit_facts=True
            )

            self.assertEqual(accepted, [str(source)])
            self.assertEqual(pixels.shape, (1, 3, 224, 224))
            payload = base64.b64decode(samples[str(source)])
            with Image.open(io.BytesIO(payload)) as sample:
                self.assertEqual(sample.format, "PNG")
                self.assertLessEqual(max(sample.size), 96)
                self.assertEqual(sample.size, (96, 48))


if __name__ == "__main__":
    unittest.main()
