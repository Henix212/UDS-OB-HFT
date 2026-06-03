"""Download Kaggle LOB CSVs into data/raw_data/ (requires kagglehub + API credentials)."""

import kagglehub
import os
import shutil

DATASET_HANDLE = "martinsn/high-frequency-crypto-limit-order-book-data"
DESTINATION_DIR = "data/raw_data"


def fetch_data():
    os.makedirs(DESTINATION_DIR, exist_ok=True)

    existing_files = [f for f in os.listdir(DESTINATION_DIR) if f.endswith(".csv")]
    if existing_files:
        # Skip download if CSVs are already present.
        return

    dataset_path = kagglehub.dataset_download(DATASET_HANDLE)
    for name in os.listdir(dataset_path):
        if not name.endswith(".csv"):
            continue
        source_path = os.path.join(dataset_path, name)
        dest_path = os.path.join(DESTINATION_DIR, name)
        shutil.copy2(source_path, dest_path)


if __name__ == "__main__":
    fetch_data()
