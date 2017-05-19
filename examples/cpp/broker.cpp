/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include "options.hpp"

#include <proton/connection.hpp>
#include <proton/connection_options.hpp>
#include <proton/container.hpp>
#include <proton/default_container.hpp>
#include <proton/delivery.hpp>
#include <proton/error_condition.hpp>
#include <proton/function.hpp>
#include <proton/listen_handler.hpp>
#include <proton/listener.hpp>
#include <proton/message.hpp>
#include <proton/messaging_handler.hpp>
#include <proton/receiver_options.hpp>
#include <proton/sender_options.hpp>
#include <proton/source_options.hpp>
#include <proton/target.hpp>
#include <proton/target_options.hpp>
#include <proton/thread_safe.hpp>
#include <proton/tracker.hpp>

#include <deque>
#include <iostream>
#include <map>
#include <string>

#include "fake_cpp11.hpp"

// This is a simplified model for a message broker, that only allows for messages to go to a
// single receiver.
//
// Queues are only created and never destroyed
//
// Broker Entities (that need to be individually serialised)
// QueueManager - Creates new queues, finds queues
// Queue        - Queues msgs, records subscribers, sends msgs to subscribers
// Connection   - Receives Messages from network, sends messages to network.

// Work
// FindQueue(queueName, connection) - From a Connection to the QueueManager
//     This will create the queue if it doesn't already exist and send a BoundQueue
//     message back to the connection.
// BoundQueue(queue) - From the QueueManager to a Connection
//
// QueueMsg(msg)        - From a Connection (receiver) to a Queue
// Subscribe(sender)    - From a Connection (sender) to a Queue
// Flow(sender, credit) - From a Connection (sender) to a Queue
// Unsubscribe(sender)  - From a Connection (sender) to a Queue
//
// SendMsg(msg)   - From a Queue to a Connection (sender)
// Unsubscribed() - From a Queue to a Connection (sender)

class Queue;

class Sender {
    friend class connection_handler;

    proton::sender sender_;
    proton::work_queue& work_queue_;
    std::string queue_name_;
    Queue* queue_;
    int pending_credit_;

public:
    Sender(proton::sender s) :
        sender_(s), work_queue_(s.work_queue()), queue_(0), pending_credit_(0)
    {}

    bool add(proton::work f) {
        return work_queue_.add(f);
    }


    void boundQueue(Queue* q, std::string qn);
    void sendMsg(proton::message m) {
        std::cerr << "Sender:   " << this << " sending\n";
        sender_.send(m);
    }
    void unsubscribed() {
        std::cerr << "Sender:   " << this << " deleting\n";
        delete this;
    }
};

// Queue - round robin subscriptions
class Queue {
    proton::work_queue work_queue_;
    const std::string name_;
    std::deque<proton::message> messages_;
    typedef std::map<Sender*, int> subscriptions; // With credit
    subscriptions subscriptions_;
    subscriptions::iterator current_;

    void tryToSend() {
        std::cerr << "Queue:    " << this << " tryToSend: " << subscriptions_.size();
        // Starting at current_, send messages to subscriptions with credit:
        // After each send try to find another subscription; Wrap around;
        // Finish when we run out of messages or credit.
        size_t outOfCredit = 0;
        while (!messages_.empty() && outOfCredit<subscriptions_.size()) {
            // If we got the end (or haven't started yet) start at the beginning
            if (current_==subscriptions_.end()) {
                current_=subscriptions_.begin();
            }
            // If we have credit send the message
            std::cerr << "(" << current_->second << ") ";
            if (current_->second>0) {
                std::cerr << current_->first << " ";
                proton::schedule_work(current_->first, &Sender::sendMsg, current_->first, messages_.front());
                messages_.pop_front();
                --current_->second;
                ++current_;
            } else {
                ++outOfCredit;
            }
        }
        std::cerr << "\n";
    }

public:
    Queue(proton::container& c, const std::string& n) :
        work_queue_(c), name_(n), current_(subscriptions_.end())
    {}

    bool add(proton::work f) {
        return work_queue_.add(f);
    }

    void queueMsg(proton::message m) {
        std::cerr << "Queue:    " << this << "(" << name_ << ") queueMsg\n";
        messages_.push_back(m);
        tryToSend();
    }
    void flow(Sender* s, int c) {
        std::cerr << "Queue:    " << this << "(" << name_ << ") flow: " << c << " to " << s << "\n";
        subscriptions_[s] = c;
        tryToSend();
    }
    void subscribe(Sender* s) {
        std::cerr << "Queue:    " << this << "(" << name_ << ") subscribe Sender: " << s << "\n";
        subscriptions_[s] = 0;
    }
    void unsubscribe(Sender* s) {
        std::cerr << "Queue:    " << this << "(" << name_ << ") unsubscribe Sender: " << s << "\n";
        // If we're about to erase the current subscription move on
        if (current_ != subscriptions_.end() && current_->first==s) ++current_;
        subscriptions_.erase(s);
        proton::schedule_work(s, &Sender::unsubscribed, s);
    }
};

void Sender::boundQueue(Queue* q, std::string qn) {
    std::cerr << "Sender:   " << this << " bound to Queue: " << q <<"(" << qn << ")\n";
    queue_ = q;
    queue_name_ = qn;

    proton::schedule_work(q, &Queue::subscribe, q, this);
    sender_.open(proton::sender_options().source((proton::source_options().address(queue_name_))));
    if (pending_credit_>0) {
        proton::schedule_work(queue_, &Queue::flow, queue_, this, pending_credit_);
    }
    std::cout << "sending from " << queue_name_ << std::endl;
}

class Receiver {
    friend class connection_handler;

    proton::receiver receiver_;
    proton::work_queue& work_queue_;
    Queue* queue_;
    std::deque<proton::message> messages_;

    void queueMsgs() {
        std::cerr << "Receiver: " << this << " queueing " << messages_.size() << " msgs to: " << queue_ << "\n";
        while (!messages_.empty()) {
            proton::schedule_work(queue_, &Queue::queueMsg, queue_, messages_.front());
            messages_.pop_front();
        }
    }

public:
    Receiver(proton::receiver r) :
        receiver_(r), work_queue_(r.work_queue()), queue_(0)
    {}

    bool add(proton::work f) {
        return work_queue_.add(f);
    }

    void boundQueue(Queue* q, std::string qn) {
        std::cerr << "Receiver: " << this << " bound to Queue: " << q << "(" << qn << ")\n";
        queue_ = q;
        receiver_.open(proton::receiver_options().source((proton::source_options().address(qn))));
        std::cout << "receiving to " << qn << std::endl;

        queueMsgs();
    }
};

class QueueManager {
    proton::container& container_;
    proton::work_queue work_queue_;
    typedef std::map<std::string, Queue*> queues;
    queues queues_;
    int next_id_; // Use to generate unique queue IDs.

public:
    QueueManager(proton::container& c) :
        container_(c), work_queue_(c), next_id_(0)
    {}

    bool add(proton::work f) {
        return work_queue_.add(f);
    }

    template <class T>
    void findQueue(T& connection, std::string& qn) {
        if (qn.empty()) {
            // Dynamic queue creation
            std::ostringstream os;
            os << "_dynamic_" << next_id_++;
            qn = os.str();
        }
        Queue* q = 0;
        queues::iterator i = queues_.find(qn);
        if (i==queues_.end()) {
            q = new Queue(container_, qn);
            queues_[qn] = q;
        } else {
            q = i->second;
        }
        proton::schedule_work(&connection, &T::boundQueue, &connection, q, qn);
    }

    void findQueueSender(Sender* s, std::string qn) {
        findQueue(*s, qn);
    }

    void findQueueReceiver(Receiver* r, std::string qn) {
        findQueue(*r, qn);
    }
};

class connection_handler : public proton::messaging_handler {
    QueueManager& queue_manager_;
    typedef std::map<proton::sender, Sender*> senders;
    senders senders_;
    typedef std::map<proton::receiver, Receiver*> receivers;
    receivers receivers_;

public:
    connection_handler(QueueManager& qm) :
        queue_manager_(qm)
    {}

    void on_connection_open(proton::connection& c) OVERRIDE {
        c.open();            // Accept the connection
    }

    // A sender sends messages from a queue to a subscriber.
    void on_sender_open(proton::sender &sender) OVERRIDE {
        std::string qn = sender.source().dynamic() ? "" : sender.source().address();
        Sender* s = new Sender(sender);
        senders_[sender] = s;
        proton::schedule_work(&queue_manager_, &QueueManager::findQueueSender, &queue_manager_, s, qn);
    }

    // We have credit to send a message.
    void on_sendable(proton::sender &sender) OVERRIDE {
        Sender* s = senders_[sender];

        if (s->queue_) {
            proton::schedule_work(s->queue_, &Queue::flow, s->queue_, s, sender.credit());
        } else {
            s->pending_credit_ = sender.credit();
        }
    }

    // A receiver receives messages from a publisher to a queue.
    void on_receiver_open(proton::receiver &receiver) OVERRIDE {
        std::string qname = receiver.target().address();
        if (qname == "shutdown") {
            std::cout << "broker shutting down" << std::endl;
            // Sending to the special "shutdown" queue stops the broker.
            receiver.connection().container().stop(
                proton::error_condition("shutdown", "stop broker"));
        } else {
            if (qname.empty()) {
                std::cerr << "ODD - trying to attach to a empty address\n";
            }
            Receiver* r = new Receiver(receiver);
            receivers_[receiver] = r;
            proton::schedule_work(&queue_manager_, &QueueManager::findQueueReceiver, &queue_manager_, r, qname);
        }
    }

    // A message is received.
    void on_message(proton::delivery &d, proton::message &m) OVERRIDE {
        Receiver* r = receivers_[d.receiver()];
        r->messages_.push_back(m);

        if (r->queue_) {
            r->queueMsgs();
        }
    }

    void on_session_close(proton::session &session) OVERRIDE {
        // Unsubscribe all senders that belong to session.
        for (senders::iterator i = senders_.begin(); i != senders_.end();) {
            if (i->first.session()==session) {
                Sender* s = i->second;
                if (s->queue_) {
                    proton::schedule_work(s->queue_, &Queue::unsubscribe, s->queue_, s);
                }
                senders_.erase(i++);
            } else {
                ++i;
            }
        }
    }

    void on_sender_close(proton::sender &sender) OVERRIDE {
        Sender* s = senders_[sender];
        if (s->queue_) {
            proton::schedule_work(s->queue_, &Queue::unsubscribe, s->queue_, s);
        } else {
            // TODO: Is it possible to be closed before we get the queue allocated?
            // If so, we should have a way to mark the sender deleted, so we can delete
            // on queue binding
        }
        senders_.erase(sender);
    }

    void on_error(const proton::error_condition& e) OVERRIDE {
        std::cerr << "error: " << e.what() << std::endl;
    }

    // The container calls on_transport_close() last.
    void on_transport_close(proton::transport&) OVERRIDE {
        // Unsubscribe all senders.
        for (senders::iterator i = senders_.begin(); i != senders_.end(); ++i) {
            Sender* s = i->second;
            if (s->queue_) {
                proton::schedule_work(s->queue_, &Queue::unsubscribe, s->queue_, s);
            }
        }
        delete this;            // All done.
    }
};

class broker {
  public:
    broker(const std::string addr) :
        container_("mt_broker"), queues_(container_), listener_(queues_)
    {
        container_.listen(addr, listener_);
        std::cout << "broker listening on " << addr << std::endl;
    }

    void run() {
        container_.run(/* std::thread::hardware_concurrency() */);
    }

  private:
    struct listener : public proton::listen_handler {
        listener(QueueManager& c) : queues_(c) {}

        proton::connection_options on_accept() OVERRIDE{
            return proton::connection_options().handler(*(new connection_handler(queues_)));
        }

        void on_error(const std::string& s) OVERRIDE {
            std::cerr << "listen error: " << s << std::endl;
            throw std::runtime_error(s);
        }
        QueueManager& queues_;
    };

    proton::container container_;
    QueueManager queues_;
    listener listener_;
};

int main(int argc, char **argv) {
    // Command line options
    std::string address("0.0.0.0");
    example::options opts(argc, argv);

    opts.add_value(address, 'a', "address", "listen on URL", "URL");

    try {
        opts.parse();
        broker(address).run();
        return 0;
    } catch (const example::bad_option& e) {
        std::cout << opts << std::endl << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "broker shutdown: " << e.what() << std::endl;
    }
    return 1;
}
