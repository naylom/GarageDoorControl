/**
 * @file ConfigStorage.cpp
 * @brief Persists and retrieves configuration using BlobStorage library.
 * @details Uses the cross-platform BlobStorage library which automatically selects
 *          the appropriate backend (FlashStorage on SAMD) for persistent storage
 *          of WiFi credentials and UDP port settings.
 *
 * Author: (c) M. Naylor 2025
 *
 * History:
 *     Ver 1.0        Initial version
 */

#include "ConfigStorage.h"

#include "Display.h"

#include <BlobStorageFactory.h>
#include <TypedBlobStorage.h>

namespace ConfigStorage
{
// Static storage instance using TypedBlobStorage for type safety
static TypedBlobStorage<GarageConfig>* storage = nullptr;

/**
 * @brief Initializes the storage backend.
 * @return True when the storage layer is ready for use.
 * @details Creates and initializes the platform-appropriate storage backend
 *          using BlobStorageFactory. This automatically selects the correct
 *          storage mechanism for SAMD platforms.
 */
bool begin ()
{
	// Create storage instance if not already created
	if ( !storage )
	{
		BlobStorage* backend = BlobStorageFactory::create ( "garageconfig", sizeof ( GarageConfig ) );
		if ( !backend )
		{
			Error ( F ( "BlobStorage: Platform not supported" ) );
			return false;
		}
		storage = new TypedBlobStorage<GarageConfig> ( backend, true );
	}

	// Initialize storage
	BlobStorage::Error err = storage->begin();
	if ( err != BlobStorage::SUCCESS )
	{
		Error ( F ( "BlobStorage: Initialization failed" ) );
		return false;
	}

	Info ( F ( "BlobStorage: Initialized successfully" ) );
	return true;
}

/**
 * @brief Loads persisted configuration into the provided struct.
 * @param cfg Reference to receive the configuration.
 * @return True if valid data was read; false when missing or invalid.
 * @details Reads configuration from persistent storage. Returns false if
 *          storage is not initialized, data doesn't exist, or data is invalid.
 */
bool load ( GarageConfig& cfg )
{
	if ( !storage )
	{
		Error ( F ( "BlobStorage: Not initialized" ) );
		return false;
	}

	BlobStorage::Error err = storage->read ( cfg );

	// Return true only if read succeeded AND the config is marked valid
	if ( err == BlobStorage::SUCCESS && cfg.valid )
	{
		Info ( F ( "BlobStorage: Configuration loaded successfully" ) );
		return true;
	}

	Info ( F ( "BlobStorage: No valid configuration found" ) );
	return false;
}

/**
 * @brief Saves configuration to persistent storage.
 * @param cfg Configuration to persist.
 * @return True on successful write.
 * @details Writes the configuration struct to persistent storage.
 */
bool save ( const GarageConfig& cfg )
{
	if ( !storage )
	{
		Error ( F ( "BlobStorage: Not initialized" ) );
		return false;
	}

	BlobStorage::Error err = storage->write ( cfg );
	if ( err == BlobStorage::SUCCESS )
	{
		Info ( F ( "BlobStorage: Configuration saved successfully" ) );
		return true;
	}

	Error ( F ( "BlobStorage: Failed to save configuration" ) );
	return false;
}

/**
 * @brief Clears stored configuration.
 * @return True when clearing succeeded or storage already empty.
 * @details Removes the stored configuration from persistent storage.
 */
bool clear ()
{
	if ( !storage )
	{
		Error ( F ( "BlobStorage: Not initialized" ) );
		return false;
	}

	BlobStorage::Error err = storage->clear();
	if ( err == BlobStorage::SUCCESS )
	{
		Info ( F ( "BlobStorage: Configuration cleared successfully" ) );
		return true;
	}

	Error ( F ( "BlobStorage: Failed to clear configuration" ) );
	return false;
}
}  // namespace ConfigStorage
