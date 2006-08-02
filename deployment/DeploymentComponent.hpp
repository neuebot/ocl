
#include "rtt/TaskContext.hpp"
#include "rtt/PropertyBag.hpp"
#include "rtt/Attribute.hpp"

#include "rtt/PropertyLoader.hpp"


namespace Orocos
{

    /**
     * A Component for deploying (configuring) other components in an
     * application. It allows to create connections between components,
     * load the properties for components and rename the ports of components,
     * such that automatic port connection can take place.
     */
    class DeploymentComponent
        : public TaskContext
    {
        PropertyBag root;
        // STRUCTURE:
        //         Property<PropertyBag> component;
        //         //         Property<std::string> property-source;
        
        //         Property<PropertyBag> ports;
        //         //         Property<std::string> portname; // value = connection_name
        
        //         Property<std::string> peername;

        Property<std::string> configurationfile;
        Attribute<bool> validConfig;

        struct ConnectionData {
            typedef std::vector<PortInterface*> Ports;
            Ports ports;
        };

        typedef std::map<std::string, ConnectionData> ConMap;
        ConMap conmap;
    public:
        DeploymentComponent(std::string name = "Configurator")
            : TaskContext(name),
              configurationfile("ConfigFile", "Name of the configuration file.", name+".cpf"),
              validConfig("Valid", false)
        {
            this->properties()->addProperty( &configurationfile );
            this->attributes()->addAttribute( &configurationfile );
        }

        /** 
         * Establish a bidirectional connection between two tasks.
         * 
         * @param one The first task to connect
         * @param other The second task to connect
         * 
         * @return true if both tasks are peers of this component and
         * could be connected.
         */
        bool connectPeers(const std::string& one, const std::string& other)
        {
            TaskContext* t1 = this->getPeer(one);
            TaskContext* t2 = this->getPeer(other);
            if (!t1) {
                Logger::log() <<Logger::Error<< "No such peer: "<<one<<Logger::endl;
                return false;
            }
            if (!t2) {
                Logger::log() <<Logger::Error<< "No such peer: "<<other<<Logger::endl;
                return false;
            }
            return t1->connectPeers(t2);
        }
        /** 
         * Establish a uni directional connection form one task to another
         * 
         * @param from The component where the connection starts.
         * @param to The component where the connection ends.
         * 
         * @return true if both tasks are peers of this component and
         * a connection could be created.
         */
        bool addPeer(const std::string& from, const std::string& to)
        {
            TaskContext* t1 = this->getPeer(from);
            TaskContext* t2 = this->getPeer(to);
            if (!t1) {
                Logger::log() <<Logger::Error<< "No such peer: "<<from<<Logger::endl;
                return false;
            }
            if (!t2) {
                Logger::log() <<Logger::Error<< "No such peer: "<<to<<Logger::endl;
                return false;
            }
            return t1->addPeer(t2);
        }

        /** 
         * Load a configuration from disk. The 'ConfigFile' property is used to
         * locate the file. This does not apply the configuration yet on the
         * components.
         * @see configurePeers to configure the peer components with the loaded
         * configuration.
         * 
         * @return true if the configuration could be read and was valid.
         */
        bool loadConfiguration()
        {
            Logger::In in("DeploymentComponent::loadConfiguration");
#ifndef OROPKG_CORELIB_PROPERTIES_MARSHALLING
            Logger::log() <<Logger::Error << "No Property Demarshaller configured !" << Logger::endl;
            return false;
    
#else

            Logger::log() <<Logger::Info << "Loading '" <<configurationfile.get()"'."<< Logger::endl;
            // demarshalling failures:
            bool failure = false;  
            // semantic failures:
            bool valid = validConfig;
            OROCLS_CORELIB_PROPERTIES_DEMARSHALLING_DRIVER* demarshaller = 0;
            try
                {
                    demarshaller = new OROCLS_CORELIB_PROPERTIES_DEMARSHALLING_DRIVER (configurationfile);
                } catch (...) {
                    Logger::log() << Logger::Error
                                  << "Could not open file "<< configurationfile << Logger::endl;
                    return false;
                }
            try {
                vector<CommandInterface*> assignComs;

                if ( demarshaller->deserialize( root ) )
                    {
                        valid = true;
                        conmap.clear();
                        Logger::Log() <<Logger::Info<<"Validating new configuration..."<<Logger::endl;
                        if ( root.empty() ) {
                            Logger::log() << Logger::Error
                                          << "Configuration was empty !" <<Logger::endl;
                            valid = false;
                        }
                        PropertyBag::Names nams = root.list();

                        for (PropertyBag::Names::iterator it= nams.begin(); nams.end()) {
                            // Check if it is a propertybag.
                            Property<PropertyBag>* comp = root.getProperty<PropertyBag>(*it);
                            if ( comp == 0 ) {
                                Logger::log() << Logger::Error
                                              << "Property '"<< *it <<"' is not a PropertyBag." << Logger::endl;
                                valid = false;
                            }
                            // Check if we know this component.
                            TaskContext* c = this->getPeer( *it );
                            if ( !c ) {
                                Logger::log() << Logger::Warning
                                              << "Could not configure '"<< *it <<"': No such peer."<< Logger::endl;
                                valid = false;
                            }
                            // set PropFile name if present
                            string filename = *it + ".cpf";
                            if ( comp->get()->getProperty("PropFile") )
                                if ( !comp->get()->getProperty<string>("PropFile") ) {
                                    valid = false;
                                }

                            // connect ports.
                            Property<PropertyBag>* ports = comp->getProperty<PropertyBag>("Ports");
                            if ( ports != 0 ) {
                                for (PropertyBag::Names::iterator pit= ports->begin(); ports->end()) {
                                    PortInterface* p = comp->ports()->getPort(*pit);
                                    if ( !p ) {
                                        Logger::log() << Logger::Error
                                                      << "Component '"<< comp->getName() <<"' does not have a Port '"<<*pit<<"'." << Logger::endl;
                                        valid = false;
                                    }
                                    if ( ports->getProperty<std::string>(*pit) == 0) {
                                        Logger::log() << Logger::Error
                                                      << "Property '"<< *pit <<"' is not of type 'string'." << Logger::endl;
                                        valid = false;
                                    }
                                    // store the port
                                    conmap[ports->getProperty<std::string>(*pit)->get()].ports.push_back( p );
                                }
                            } else {
                                valid = false;
                            }

                            // Setup the connections from this component to the others.
                            for (PropertyBag::Properties::iterator it= comp->begin(); comp->end()) {
                                if ( (*it)->getName() == "Peer" ) {
                                    Property<std::string>* nm = Property<std::string>::narrow(*it);
                                    if ( !nm ) {
                                        Logger::log()<<Logger::Error<<"Property 'Peer' does not have type 'string'."<<Logger::endl;
                                        valid = false;
                                    }
                                    if ( this->getPeer( nm->value() ) == 0 ) {
                                        Logger::log()<<Logger::Error<<"No such Peer:"<<nm->value()<<Logger::endl;
                                        valid = false;
                                    }
                                }
                            }
                
                        }
                        if ( !valid )
                            deleteProperties( root );
                    }
                else
                    {
                        Logger::log() << Logger::Error
                                      << "Some error occured while parsing "<< configurationfile.c_str() <<Logger::endl;
                        failure = true;
                        deleteProperties( root );
                        conmap.clear();
                    }
            } catch (...)
                {
                    Logger::log() << Logger::Error
                                  << "Uncaught exception in deserialise !"<< Logger::endl;
                    failure = true;
                }
            delete demarshaller;
            validConfig.set( valid );
            return !failure && valid;
#endif // OROPKG_CORELIB_PROPERTIES_MARSHALLING
        }

        /** 
         * Configure the components with a loaded configuration.
         * 
         * 
         * @return true if all components could be succesfully configured.
         */
        bool configureComponents()
        {
            if ( root.empty() ) {
                Logger::log() << Logger::Error
                              << "No configuration loaded !" <<Logger::endl;
                return false;
            }
            PropertyBag::Names nams = root.list();

            // Load all property files into the components.
            for (PropertyBag::Names::iterator it= nams.begin(); nams.end()) {
                Property<PropertyBag>* comp = root.getProperty<PropertyBag>(*it);

                // set PropFile name if present
                string filename = *it + ".cpf";
                if ( comp->get()->getProperty<string>("PropFile") )
                    filename = comp->get()->getProperty<string>("PropFile")->get();

                PropertyLoader pl;
                TaskContext* peer = this->getPeer( *it );
                assert( peer );
                bool ret = pl.configure( filename, peer, true ); // strict:true 
                if (!ret) {
                    log(Error) << "Failed to configure properties for component "<<*it<<endlog();
                }
                peer->disconnect();
            }

            // Create data port connections:
            for(ConMap::iterator it = conmap.begin(); it != conmap.end(); ++it) {
                if ( it->second.ports.size() < 2 ){
                    log(Error) << "Can not form connection "<<it->first<<" with only one Port."<< endlog();
                    continue;
                }
                // first find a write and a read port.
                PortInterface* writer = 0, reader = 0;
                ConMap::Ports::iterator p = it->second.ports.begin();
                while (p != it->second.ports.end() && (writer == 0 || reader == 0) ) {
                    if ( (*p)->getPortType() == PortInterface::WritePort )
                        writer = (*p)->clone();
                    else
                        if ( (*p)->getPortType() == PortInterface::ReadPort )
                            reader = (*p)->clone();
                    else
                        if ( (*p)->getPortType() == PortInterface::ReadWritePort )
                            if (reader == 0)
                                reader = (*p)->clone();
                            else
                                writer = (*p)->clone();
                    ++p;
                }
                // Idea is: create a clone or anticlone of a port
                // use that one to setup the connection object
                // then dispose it again.
                if ( writer == 0 ) {
                    log(Warning) << "Connecting only read-ports in connection " << it->first << endlog();
                    // solve this issue by using a temporary anti-port.
                    writer = it->second.ports.front()->antiPort();
                }
                if ( reader == 0 ) {
                    log(Warning) << "Connecting only write-ports in connection " << it->first << endlog();
                    // make an anticlone of a writer
                    reader = it->second.ports.front()->antiPort();
                }
                
                log(Info) << "Creating Connection "<<it->first<<":"<<endlog();
                log(Info) << "Connecting Port "<< writer->getName() <<" to Port " << reader->getName()<<endlog();
                ConnectionInterface::shared_ptr con = writer->createConnection( reader );
                // connect all ports to connection
                p = it->second.ports.begin();
                while (p != it->second.ports.end() ) {
                    if ((*p)->connectTo( con ) == false) {
                        log(Error) << "Could not connect Port "<< (*p)->getName() << " to connection " <<it->first<<endlog();
                        if ((*p)->connected())
                            log(Error) << "Port "<< (*p)->getName() << " already connected !"<<endlog();
                        else
                            log(Error) << "Port "<< (*p)->getName() << " has wrong type !"<<endlog();
                    } else
                        log(Info) << "Connected Port "<< (*p)->getName() <<" to connection " << it->first <<endlog();

                }
                // writer,reader was a clone or anticlone.
                delete writer;
                delete reader;
            }

            // Setup the connections from each component to the others.
            for (PropertyBag::Names::iterator nit= nams.begin(); nams.end()) {
                Property<PropertyBag>* comp = root.getProperty<PropertyBag>(*nit);
                for (PropertyBag::Properties::iterator it= comp->begin(); comp->end()) {
                    if ( (*it)->getName() == "Peer" ) {
                        Property<std::string>* nm = Property<std::string>::narrow(*it);
                        this->addPeer( comp->getName(), nm->value() );
                    }
                }
                
            }
            return true;
        }

        /**
         * Configure a component by loading the property file 'name.cpf' for component with
         * name \a name.
         * @param name The name of the component to configure.
         * The file used will be 'name.cpf'.
         * @return true if the component is known and the file could be
         * read.
         */
        bool configure(std::string name)
        {
            return configureComponent( name,  name + ".cpf" );
        }

        /** 
         * Configure a component by loading a property file.
         * 
         * @param name The name of the component to configure
         * @param filename The filename where the configuration is in.
         * 
         * @return true if the component is known and the file could be
         * read.
         */
        bool configureFromFile(std::string name, std::string filename)
        {
            TaskContext* c = this->getPeer(name);
            if (!c) {
                Logger::log()<<Logger::Error<<"No such peer to configure: "<<name<<Logger::endl;
                return false;
            }
            
            PropertyLoader pl;
            return pl.configure( filename, c, true ); // strict:true 
        }

    };


}