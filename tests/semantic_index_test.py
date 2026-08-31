#!/usr/bin/env python3

import importlib.machinery
import importlib.util
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


if __name__ == "__main__":
    unittest.main()
