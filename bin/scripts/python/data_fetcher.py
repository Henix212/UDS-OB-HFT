import kagglehub
import os
import shutil

def fetch_data():
    dataset_handle = "martinsn/high-frequency-crypto-limit-order-book-data"
    dossier_destination = "data/raw_data"

    os.makedirs(dossier_destination, exist_ok=True)

    fichiers_existants = [f for f in os.listdir(dossier_destination) if f.endswith('.csv')]

    if len(fichiers_existants) > 0:
        pass
    else:
        chemin_dataset = kagglehub.dataset_download(dataset_handle)
        
        fichiers_cache = os.listdir(chemin_dataset)
        
        for f in fichiers_cache:
            if f.endswith('.csv'):
                chemin_source = os.path.join(chemin_dataset, f)
                chemin_dest = os.path.join(dossier_destination, f)
                
                shutil.copy2(chemin_source, chemin_dest)

if __name__ == "__main__":
    fetch_data()